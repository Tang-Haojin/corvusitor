#include "connection_builder.h"
#include <iostream>
#include <set>
#include <climits>

std::vector<PortConnection> ConnectionBuilder::build(const std::vector<ModuleInfo>& modules) {
  std::vector<PortConnection> connections;

  std::cout << "Building connections for " << modules.size() << " modules..." << std::endl;

  // Group by port name (single pass, record module info)
  auto port_groups = group_ports_by_name(modules);

  std::cout << "Found " << port_groups.size() << " unique port names" << std::endl;

  // Build connections for each port group
  size_t valid_connections = 0;
  size_t top_level_inputs = 0;
  size_t top_level_outputs = 0;
  size_t ignored_seq_external_outputs = 0;

  for (const auto& pair : port_groups) {
    const PortGroup& group = pair.second;

  // Validate connection legality
    std::string error_msg;
    if (!validate_connection(group, error_msg)) {
      std::cerr << "Warning: Invalid connection for port '"
                << group.port_name << "': " << error_msg << std::endl;
      continue;
    }

  // Check if there is a driver
    bool has_driver = !group.driver_ports.empty();
    bool has_receiver = !group.receiver_ports.empty();

    if (!has_driver && !has_receiver) {
      continue; // No driver and no receiver, skip
    }

  // Handle top-level input (no driver)
    if (!has_driver) {
      // Check if all receiver modules are COMB
      bool has_non_comb_receiver = false;
      for (const auto* recv_module : group.receiver_modules) {
        if (recv_module->type != ModuleType::COMB) {
          has_non_comb_receiver = true;
          std::cerr << "Warning: Top-level input port '" << group.port_name
               << "' connected to non-COMB module '" << recv_module->instance_name
               << "' (type: " << recv_module->get_type_str() << ")" << std::endl;
        }
      }
      if (has_non_comb_receiver) {
        continue;
      }

      // Create top-level input connection
      PortConnection conn;
      conn.port_name = group.port_name;
      conn.driver_module = nullptr;
      conn.receiver_modules = group.receiver_modules;
      conn.is_top_level_input = true;
      conn.is_top_level_output = false;
      conn.width = group.receiver_ports[0]->get_width();
      conn.width_type = group.receiver_ports[0]->width_type;

      connections.push_back(conn);
      valid_connections++;
      top_level_inputs++;
      continue;
    }

  // Match each receiver port to a suitable driver
  // Record connected driver modules (for top-level output detection)
    std::set<const ModuleInfo*> connected_drivers;
  // Record each receiver module's selected driver (to avoid duplicate connections)
    std::map<const ModuleInfo*, const ModuleInfo*> receiver_to_driver;

    for (size_t i = 0; i < group.receiver_ports.size(); i++) {
      const ModuleInfo* recv_module = group.receiver_modules[i];

      // Find a suitable driver source
      const PortInfo* selected_driver_port = nullptr;
      const ModuleInfo* selected_driver_module = nullptr;

      if (recv_module->type == ModuleType::SEQ) {
        // SEQ module: strictly match COMB output by partition ID
        for (size_t j = 0; j < group.driver_ports.size(); j++) {
          const ModuleInfo* driver_module = group.driver_modules[j];
          if (driver_module->type == ModuleType::COMB &&
            driver_module->partition_id == recv_module->partition_id) {
            selected_driver_port = group.driver_ports[j];
            selected_driver_module = driver_module;
            break;
          }
        }
      } else if (recv_module->type == ModuleType::COMB) {
        // COMB module: prefer SEQ output of same partition (feedback path)
        // First try to match same partition
        for (size_t j = 0; j < group.driver_ports.size(); j++) {
          const ModuleInfo* driver_module = group.driver_modules[j];
          if (driver_module->type == ModuleType::SEQ &&
            driver_module->partition_id == recv_module->partition_id) {
            selected_driver_port = group.driver_ports[j];
            selected_driver_module = driver_module;
            break;
          }
        }
        // If no SEQ of same partition, select any SEQ (prefer smallest partition ID)
        if (selected_driver_module == nullptr) {
          int min_partition = INT_MAX;
          for (size_t j = 0; j < group.driver_ports.size(); j++) {
            const ModuleInfo* driver_module = group.driver_modules[j];
            if (driver_module->type == ModuleType::SEQ &&
              driver_module->partition_id < min_partition) {
              min_partition = driver_module->partition_id;
              selected_driver_port = group.driver_ports[j];
              selected_driver_module = driver_module;
            }
          }
        }
        // If still no SEQ, try EXTERNAL
        if (selected_driver_module == nullptr) {
          for (size_t j = 0; j < group.driver_ports.size(); j++) {
            const ModuleInfo* driver_module = group.driver_modules[j];
            if (driver_module->type == ModuleType::EXTERNAL) {
              selected_driver_port = group.driver_ports[j];
              selected_driver_module = driver_module;
              break;
            }
          }
        }
      } else if (recv_module->type == ModuleType::EXTERNAL) {
        // EXTERNAL module: select COMB with smallest partition ID
        int min_partition = INT_MAX;
        for (size_t j = 0; j < group.driver_ports.size(); j++) {
          const ModuleInfo* driver_module = group.driver_modules[j];
          if (driver_module->type == ModuleType::COMB &&
            driver_module->partition_id < min_partition) {
            min_partition = driver_module->partition_id;
            selected_driver_port = group.driver_ports[j];
            selected_driver_module = driver_module;
          }
        }
      }

      // If a matching driver is found, create connection
      if (selected_driver_module != nullptr) {
        // Check if this (driver, receiver) pair is already connected
        auto it = receiver_to_driver.find(recv_module);
        if (it == receiver_to_driver.end() || it->second != selected_driver_module) {
          PortConnection conn;
          conn.port_name = group.port_name;
          conn.driver_module = selected_driver_module;
          conn.receiver_modules.push_back(recv_module);
          conn.is_top_level_input = false;
          conn.is_top_level_output = false;
          conn.width = selected_driver_port->get_width();
          conn.width_type = selected_driver_port->width_type;

          connections.push_back(conn);
          valid_connections++;

          connected_drivers.insert(selected_driver_module);
          receiver_to_driver[recv_module] = selected_driver_module;
        }
      }
    }

  // Handle top-level output: only if all COMB drivers are not connected
    bool has_any_comb_connected = false;
    for (const auto* driver_module : group.driver_modules) {
      if (driver_module->type == ModuleType::COMB &&
        connected_drivers.find(driver_module) != connected_drivers.end()) {
        has_any_comb_connected = true;
        break;
      }
    }

    if (!has_any_comb_connected) {
      // Check if there is a COMB driver
      for (size_t j = 0; j < group.driver_ports.size(); j++) {
        const ModuleInfo* driver_module = group.driver_modules[j];
        if (driver_module->type == ModuleType::COMB) {
          // Create top-level output connection
          PortConnection conn;
          conn.port_name = group.port_name;
          conn.driver_module = driver_module;
          conn.receiver_modules.clear();
          conn.is_top_level_input = false;
          conn.is_top_level_output = true;
          conn.width = group.driver_ports[j]->get_width();
          conn.width_type = group.driver_ports[j]->width_type;

          connections.push_back(conn);
          valid_connections++;
          top_level_outputs++;
        }
      }
    }

  // Count ignored SEQ/EXTERNAL outputs
    for (const auto* driver_module : group.driver_modules) {
      if (driver_module->type != ModuleType::COMB &&
        connected_drivers.find(driver_module) == connected_drivers.end()) {
        ignored_seq_external_outputs++;
      }
    }
  }

  std::cout << "Built " << valid_connections << " valid connections" << std::endl;
  std::cout << "  - Top-level inputs:  " << top_level_inputs << " (from COMB modules)" << std::endl;
  std::cout << "  - Top-level outputs: " << top_level_outputs << " (from COMB modules)" << std::endl;
  std::cout << "  - Internal signals:  " << (valid_connections - top_level_inputs - top_level_outputs) << std::endl;
  if (ignored_seq_external_outputs > 0) {
    std::cout << "  - Ignored outputs:   " << ignored_seq_external_outputs
              << " (from SEQ/EXTERNAL modules)" << std::endl;
  }

  return connections;
}

// Optimized version: single pass, build port groups and module mapping (supports multiple drivers)
std::map<std::string, PortGroup>
ConnectionBuilder::group_ports_by_name(const std::vector<ModuleInfo>& modules) {
  std::map<std::string, PortGroup> groups;

  // Traverse all ports of all modules
  for (const auto& module : modules) {
    for (const auto& port : module.ports) {
      // Get or create port group
      PortGroup& group = groups[port.name];

      // Initialize on first creation
      if (group.port_name.empty()) {
        group.port_name = port.name;
      }

      // Classify by direction
      if (port.direction == PortDirection::OUTPUT) {
        // Output port as driver (supports multiple)
        group.driver_ports.push_back(&port);
        group.driver_modules.push_back(&module);
      } else {
        // Input port as receiver
        group.receiver_ports.push_back(&port);
        group.receiver_modules.push_back(&module);
      }
    }
  }

  return groups;
}

bool ConnectionBuilder::validate_connection(
  const PortGroup& group,
  std::string& error_msg
) {
  // Check bit width consistency
  const PortInfo* reference_port = nullptr;

  // Prefer driver port as reference
  if (!group.driver_ports.empty()) {
    reference_port = group.driver_ports[0];
  } else if (!group.receiver_ports.empty()) {
    reference_port = group.receiver_ports[0];
  } else {
    error_msg = "No ports in group";
    return false;
  }

  // Check all driver ports for bit width consistency
  for (const auto* driver_port : group.driver_ports) {
    if (!is_width_compatible(*reference_port, *driver_port)) {
      error_msg = "Driver width mismatch: " +
                   std::to_string(reference_port->get_width()) + " vs " +
                   std::to_string(driver_port->get_width());
      return false;
    }
  }

  // Check all receiver ports for bit width consistency with driver port
  for (const auto* recv_port : group.receiver_ports) {
    if (!is_width_compatible(*reference_port, *recv_port)) {
      error_msg = "Receiver width mismatch: " +
                   std::to_string(reference_port->get_width()) + " vs " +
                   std::to_string(recv_port->get_width());
      return false;
    }
  }

  return true;
}

bool ConnectionBuilder::is_width_compatible(const PortInfo& p1, const PortInfo& p2) {
  // Check if bit width is the same
  if (p1.get_width() != p2.get_width()) {
    return false;
  }

  // Check if type is compatible
  if (p1.width_type != p2.width_type) {
    return false;
  }

  return true;
}

namespace {
const PortInfo* find_port(const ModuleInfo* module, const std::string& port_name) {
  if (!module) return nullptr;
  for (const auto& port : module->ports) {
    if (port.name == port_name) {
      return &port;
    }
  }
  return nullptr;
}

ClassifiedConnection make_connection(const PortConnection& conn) {
  ClassifiedConnection classified;
  classified.port_name = conn.port_name;
  classified.width = conn.width;
  classified.width_type = conn.width_type;
  if (conn.driver_module) {
    classified.driver.module = conn.driver_module;
    classified.driver.port = find_port(conn.driver_module, conn.port_name);
  }
  for (const auto* recv : conn.receiver_modules) {
    SignalEndpoint endpoint;
    endpoint.module = recv;
    endpoint.port = find_port(recv, conn.port_name);
    classified.receivers.push_back(endpoint);
  }
  return classified;
}
} // namespace

ConnectionAnalysis ConnectionBuilder::analyze(const std::vector<ModuleInfo>& modules) {
  ConnectionAnalysis analysis;
  auto connections = build(modules);

  // Initialize partition buckets
  for (const auto& mod : modules) {
    if (mod.type != ModuleType::EXTERNAL) {
      analysis.partitions[mod.partition_id];
    }
  }

  auto warn = [&](const std::string& msg) {
    analysis.warnings.push_back(msg);
  };

  for (const auto& conn : connections) {
    // Prepare a reusable view of the full connection
    auto base = make_connection(conn);

    if (conn.is_top_level_input) {
      analysis.top_inputs.push_back(base);
      continue;
    }
    if (conn.is_top_level_output) {
      analysis.top_outputs.push_back(base);
      continue;
    }
    if (!conn.driver_module) {
      warn("Connection without driver: " + conn.port_name);
      continue;
    }

    auto driver_type = conn.driver_module->type;
    auto driver_pid = conn.driver_module->partition_id;

    // Split per receiver to classify cleanly
    for (const auto* recv : conn.receiver_modules) {
      ClassifiedConnection single = base;
      single.receivers.clear();
      SignalEndpoint recv_ep;
      recv_ep.module = recv;
      recv_ep.port = find_port(recv, conn.port_name);
      single.receivers.push_back(recv_ep);

      if (!recv) {
        warn("Null receiver in connection: " + conn.port_name);
        continue;
      }

      if (driver_type == ModuleType::COMB) {
        if (recv->type == ModuleType::SEQ) {
          if (recv->partition_id != driver_pid) {
            warn("Illegal COMB->SEQ across partitions for port '" + conn.port_name + "'");
            continue;
          }
          analysis.partitions[driver_pid].local_c_to_s.push_back(single);
        } else if (recv->type == ModuleType::EXTERNAL) {
          analysis.external_inputs.push_back(single); // comb -> external (Ei)
        } else {
          warn("Unsupported COMB receiver type for port '" + conn.port_name + "'");
        }
      } else if (driver_type == ModuleType::SEQ) {
        if (recv->type != ModuleType::COMB) {
          warn("SEQ driver port '" + conn.port_name + "' targets non-COMB receiver");
          continue;
        }
        if (recv->partition_id == driver_pid) {
          analysis.partitions[driver_pid].local_s_to_c.push_back(single);
        } else {
          analysis.partitions[driver_pid].remote_s_to_c.push_back(single);
        }
      } else if (driver_type == ModuleType::EXTERNAL) {
        if (recv->type == ModuleType::COMB) {
          analysis.external_outputs.push_back(single); // external -> comb (Eo)
        } else {
          warn("EXTERNAL driver port '" + conn.port_name + "' targets non-COMB receiver");
        }
      } else {
        warn("Unknown driver type for port '" + conn.port_name + "'");
      }
    }
  }

  return analysis;
}
