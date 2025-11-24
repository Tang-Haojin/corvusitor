#include "code_generator.h"
#include "module_parser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <set>
#include <map>
#include <dirent.h>
#include <sys/stat.h>

CodeGenerator::CodeGenerator(const std::string& modules_dir)
  :modules_dir_(modules_dir)
  ,conn_builder_(nullptr)
  ,total_connections_(0)
  ,vlwide_ports_(0)
  ,top_outputs_(0) {
}

bool CodeGenerator::load_data() {
  std::cout << "\n=== Loading Module Data ===" << std::endl;

  std::vector<ModuleDiscoveryResult> discovery_results;

  // Use ModuleDiscoveryManager to discover modules from all simulators
  ModuleDiscoveryManager manager;
  discovery_results = manager.discover_all_modules(modules_dir_);
  manager.print_discovery_statistics();

  if (discovery_results.empty()) {
    std::cerr << "No modules found in directory: " << modules_dir_ << std::endl;
    return false;
  }

  // Parse each module header using appropriate parser for each simulator
  std::vector<ModuleInfo> modules_list;
  
  for (const auto& result : discovery_results) {
    std::cout << "  Parsing " << result.header_path << " [" << result.simulator_name << "]..." << std::endl;
    
    // Create appropriate parser for this simulator
    std::unique_ptr<ModuleParser> parser = ModuleParserFactory::create(result.simulator_name);
    if (!parser) {
      std::cerr << "Failed to create parser for simulator: " << result.simulator_name << std::endl;
      return false;
    }
    
    ModuleInfo info = parser->parse(result.header_path);

    if (info.ports.empty()) {
      std::cerr << "Failed to parse module: " << result.module_name << std::endl;
      return false;
    }

    modules_[result.module_name] = info;
    modules_list.push_back(info);

    std::cout << "    -> " << info.ports.size() << " ports" << std::endl;
  }

  // Build connections
  std::cout << "\n=== Building Connections ===" << std::endl;
  conn_builder_ = new ConnectionBuilder();
  auto connections = conn_builder_->build(modules_list);
  total_connections_ = connections.size();

  std::cout << "  Total connections: " << total_connections_ << std::endl;

  return true;
}

std::string CodeGenerator::generate_assignment(const PortConnection& conn) {
  std::ostringstream oss;

  // Check if this is a top-level input
  if (conn.is_top_level_input) {
    // TOP -> COMB: read from public member variable
    for (const auto* receiver : conn.receiver_modules) {
      std::string recv_ptr = "m_" + receiver->instance_name;
      oss << "    " << recv_ptr << "->" << conn.port_name
          << " = " << conn.port_name << ";\n";
    }
    return oss.str();
  }

  // Check if this is a top-level output
  if (conn.is_top_level_output) {
    // COMB -> TOP: write to public member variable
    if (conn.driver_module) {
      std::string driver_ptr = "m_" + conn.driver_module->instance_name;
      oss << "    " << conn.port_name << " = "
          << driver_ptr << "->" << conn.port_name << ";\n";
    }
    return oss.str();
  }

  // Normal connection: driver -> receivers
  if (!conn.driver_module || conn.receiver_modules.empty()) {
    return "    // ERROR: Invalid connection\n";
  }

  std::string driver_ptr = "m_" + conn.driver_module->instance_name;

  // Get driver port info to check type
  const PortInfo* driver_port = nullptr;
  for (const auto& p : conn.driver_module->ports) {
    if (p.name == conn.port_name) {
      driver_port = &p;
      break;
    }
  }

  if (!driver_port) {
    return "    // ERROR: Port not found in driver module\n";
  }

  // Generate assignments to all receivers
  for (const auto* receiver : conn.receiver_modules) {
    std::string recv_ptr = "m_" + receiver->instance_name;
      oss << "    " << recv_ptr << "->" << conn.port_name
          << " = " << driver_ptr << "->" << conn.port_name << ";\n";
  }

  return oss.str();
}

std::string CodeGenerator::generate_propagate_function(
  const std::vector<PortConnection>& connections,
  const std::string& function_name,
  const std::string& comment) {

  std::ostringstream oss;

  // Function header
  oss << "void VCorvusTopWrapper::" << function_name << "() {\n";
  oss << "    // " << comment << "\n";
  oss << "    // Auto-generated: " << connections.size() << " connections\n";
  oss << "\n";

  if (connections.empty()) {
    oss << "    // No connections\n";
  } else {
    // Group by module type for better organization
    std::vector<const PortConnection*> comb_to_seq;
    std::vector<const PortConnection*> seq_to_comb;
    std::vector<const PortConnection*> comb_to_ext;
    std::vector<const PortConnection*> ext_to_comb;
    std::vector<const PortConnection*> top_inputs;
    std::vector<const PortConnection*> top_outputs;

    for (const auto& conn : connections) {
      if (conn.is_top_level_input) {
        top_inputs.push_back(&conn);
      } else if (conn.is_top_level_output) {
        top_outputs.push_back(&conn);
      } else if (conn.driver_module && !conn.receiver_modules.empty()) {
        auto driver_type = conn.driver_module->type;
        auto receiver_type = conn.receiver_modules[0]->type;

        if (driver_type == ModuleType::COMB && receiver_type == ModuleType::SEQ) {
          comb_to_seq.push_back(&conn);
        } else if (driver_type == ModuleType::SEQ && receiver_type == ModuleType::COMB) {
          seq_to_comb.push_back(&conn);
        } else if (driver_type == ModuleType::COMB && receiver_type == ModuleType::EXTERNAL) {
          comb_to_ext.push_back(&conn);
        } else if (driver_type == ModuleType::EXTERNAL && receiver_type == ModuleType::COMB) {
          ext_to_comb.push_back(&conn);
        }
      }
    }

    // Generate organized code
    auto write_group = [&](const std::vector<const PortConnection*>& group, const std::string& title) {
      if (!group.empty()) {
        oss << "    // " << title << " (" << group.size() << " connections)\n";
        for (const auto* conn : group) {
          oss << generate_assignment(*conn);
        }
        oss << "\n";
      }
    };

    write_group(top_inputs, "Top-level inputs");
    write_group(comb_to_seq, "COMB -> SEQ");
    write_group(seq_to_comb, "SEQ -> COMB");
    write_group(comb_to_ext, "COMB -> EXTERNAL");
    write_group(ext_to_comb, "EXTERNAL -> COMB");
    write_group(top_outputs, "Top-level outputs");
  }

  oss << "}\n";

  return oss.str();
}

std::string CodeGenerator::generate_port_declarations(
  const std::vector<PortConnection>& top_inputs,
  const std::vector<PortConnection>& top_outputs) {

  std::ostringstream oss;

  oss << "    // ========================================================================\n";
  oss << "    // Top-level Input/Output Ports (Auto-generated)\n";
  oss << "    // Similar to Verilator-generated interface\n";
  oss << "    // ========================================================================\n";
  oss << "\n";

  // Generate input declarations
  if (!top_inputs.empty()) {
    oss << "    // Inputs (" << top_inputs.size() << " ports)\n";
    std::set<std::string> generated_inputs;

    for (const auto& conn : top_inputs) {
      if (!conn.is_top_level_input) continue;
      if (generated_inputs.find(conn.port_name) != generated_inputs.end()) continue;
      generated_inputs.insert(conn.port_name);

      // Find port type from first receiver
      if (conn.receiver_modules.empty()) continue;
      const PortInfo* port_info = nullptr;
      for (const auto& port : conn.receiver_modules[0]->ports) {
        if (port.name == conn.port_name) {
          port_info = &port;
          break;
        }
      }
      if (!port_info) continue;

      std::string cpp_type = port_info->get_cpp_type();

      // Handle VlWide types
      if (port_info->width_type == PortWidthType::VL_W) {
        oss << "    VlWide<" << port_info->array_size << "> " << conn.port_name << ";\n";
      } else {
        oss << "    " << cpp_type << " " << conn.port_name << ";\n";
      }
    }
    oss << "\n";
  }

  // Generate output declarations
  if (!top_outputs.empty()) {
    oss << "    // Outputs (" << top_outputs.size() << " ports)\n";
    std::set<std::string> generated_outputs;

    for (const auto& conn : top_outputs) {
      if (!conn.driver_module || !conn.is_top_level_output) continue;
      if (generated_outputs.find(conn.port_name) != generated_outputs.end()) continue;
      generated_outputs.insert(conn.port_name);

      // Find port type from driver module
      const PortInfo* port_info = nullptr;
      for (const auto& port : conn.driver_module->ports) {
        if (port.name == conn.port_name) {
          port_info = &port;
          break;
        }
      }
      if (!port_info) continue;

      std::string cpp_type = port_info->get_cpp_type();

      // Handle VlWide types
      if (port_info->width_type == PortWidthType::VL_W) {
        oss << "    VlWide<" << port_info->array_size << "> " << conn.port_name << ";\n";
      } else {
        oss << "    " << cpp_type << " " << conn.port_name << ";\n";
      }
    }
  }

  return oss.str();
}

// 用于顶层端口数字后缀排序
struct TopPortName {
  std::string base_name;
  size_t index;
};

TopPortName parseTopPortName(const std::string &s) {
  TopPortName result;
  int i = s.length() - 1;
  while (i >= 0 && std::isdigit(s[i])) i--;
  result.base_name = s.substr(0, i + 1);
  result.index = (i + 1 < s.length()) ? std::stoi(s.substr(i + 1)) : 0;
  return result;
}

bool compareTopPortNames(const PortConnection a, const PortConnection b) {
  TopPortName ta = parseTopPortName(a.port_name);
  TopPortName tb = parseTopPortName(b.port_name);
  if (ta.base_name != tb.base_name) return ta.base_name < tb.base_name;
  return ta.index < tb.index;
}

bool CodeGenerator::generate_all(const std::string& output_file_base) {
  std::cout << "\n=== Generating Connection Code ===" << std::endl;

  if (!conn_builder_) {
    std::cerr << "Error: Connection builder not initialized. Call load_data() first." << std::endl;
    return false;
  }

  const std::string output_cpp_file = output_file_base + ".cpp";
  const std::string output_h_file = output_file_base + ".h";

  // Open output files
  std::ofstream out_cpp(output_cpp_file);
  if (!out_cpp.is_open()) {
    std::cerr << "Failed to open output CPP file: " << output_cpp_file << std::endl;
    return false;
  }

  std::ofstream out_h(output_h_file);
  if (!out_h.is_open()) {
    std::cerr << "Failed to open output H file: " << output_h_file << std::endl;
    return false;
  }

  // Get header guard name from filename
  std::string header_guard = "VCORVUS_TOP_WRAPPER_GENERATED_H";

  // H file: Generate complete standalone header
  out_h << "// ============================================================================\n";
  out_h << "// Auto-generated by CodeGenerator\n";
  out_h << "// Multi-Module Wrapper - Standalone Header\n";
  out_h << "// Generated: " << __DATE__ << " " << __TIME__ << "\n";
  out_h << "// ============================================================================\n";
  out_h << "\n";
  out_h << "#ifndef " << header_guard << "\n";
  out_h << "#define " << header_guard << "\n";
  out_h << "\n";
  out_h << "// Verilator includes\n";
  out_h << "#include \"verilated.h\"\n";
  out_h << "\n";
  out_h << "// Auto-generated: Include all Verilator-generated module headers\n";

  // Generate include statements from modules_ keys
  for (const auto& pair : modules_) {
    const std::string& module_name = pair.first;
    out_h << "#include \"V" << module_name << ".h\"\n";
  }
  out_h << "\n";
  out_h << "// ============================================================================\n";
  out_h << "// VCorvusTopWrapper - Main wrapper class for multi-module design\n";
  out_h << "// ============================================================================\n";
  out_h << "class VCorvusTopWrapper {\n";
  out_h << "public:\n";
  out_h << "    // Constructor & Destructor\n";
  out_h << "    VCorvusTopWrapper();\n";
  out_h << "    ~VCorvusTopWrapper();\n";
  out_h << "\n";
  out_h << "    // Simulation control\n";
  out_h << "    void eval();  // Evaluate one cycle\n";
  out_h << "\n";

  // CPP file header
  out_cpp << "// ============================================================================\n";
  out_cpp << "// Auto-generated by CodeGenerator\n";
  out_cpp << "// Multi-Module Wrapper Connection Code (Implementation)\n";
  out_cpp << "// Generated: " << __DATE__ << " " << __TIME__ << "\n";
  out_cpp << "// ============================================================================\n";
  out_cpp << "\n";
  out_cpp << "#include \"" << output_h_file << "\"\n";
  out_cpp << "\n";
  out_cpp << "// ============================================================================\n";
  out_cpp << "// Constructor & Destructor\n";
  out_cpp << "// ============================================================================\n";
  out_cpp << "VCorvusTopWrapper::VCorvusTopWrapper() {\n";

  // Generate constructor initialization from modules_
  for (const auto& pair : modules_) {
    const std::string& module_name = pair.first;
    const std::string& instance_name = pair.second.instance_name;
    out_cpp << "    m_" << instance_name << " = new V" << module_name << "();\n";
  }

  out_cpp << "}\n";
  out_cpp << "\n";
  out_cpp << "VCorvusTopWrapper::~VCorvusTopWrapper() {\n";

  // Generate destructor cleanup from modules_
  for (const auto& pair : modules_) {
    const std::string& instance_name = pair.second.instance_name;
    out_cpp << "    delete m_" << instance_name << ";\n";
  }

  out_cpp << "}\n";
  out_cpp << "\n";
  out_cpp << "// ============================================================================\n";
  out_cpp << "// Main Evaluation Function\n";
  out_cpp << "// ============================================================================\n";

  // Collect modules by type
  std::vector<const ModuleInfo*> comb_modules;
  std::vector<const ModuleInfo*> seq_modules;
  std::vector<const ModuleInfo*> external_modules;

  for (const auto& pair : modules_) {
    const ModuleInfo& module = pair.second;
    switch (module.type) {
      case ModuleType::COMB:
        comb_modules.push_back(&module);
        break;
      case ModuleType::SEQ:
        seq_modules.push_back(&module);
        break;
      case ModuleType::EXTERNAL:
        external_modules.push_back(&module);
        break;
    }
  }

  out_cpp << "void VCorvusTopWrapper::eval() {\n";
  out_cpp << "    // Propagate inputs, SEQ and EXTERNAL to COMB\n";
  out_cpp << "    propagate_inputs_to_comb();\n";
  out_cpp << "    propagate_seq_to_comb();\n";
  out_cpp << "    propagate_external_to_comb();\n";
  out_cpp << "\n";

  // Evaluate COMB modules
  if (!comb_modules.empty()) {
    out_cpp << "    // Evaluate COMB modules (" << comb_modules.size() << " modules)\n";
    for (const auto* module : comb_modules) {
      out_cpp << "    m_" << module->instance_name << "->eval();\n";
    }
    out_cpp << "\n";
  }

  out_cpp << "    // Propagate COMB to SEQ, EXTERNAL and outputs\n";
  out_cpp << "    propagate_comb_to_seq();\n";
  out_cpp << "    propagate_comb_to_external();\n";
  out_cpp << "    propagate_comb_to_outputs();\n";
  out_cpp << "\n";

  // Evaluate SEQ modules
  if (!seq_modules.empty()) {
    out_cpp << "    // Evaluate SEQ modules (" << seq_modules.size() << " modules)\n";
    for (const auto* module : seq_modules) {
      out_cpp << "    m_" << module->instance_name << "->eval();\n";
    }
    out_cpp << "\n";
  }

  // Evaluate EXTERNAL modules
  if (!external_modules.empty()) {
    out_cpp << "    // Evaluate EXTERNAL modules (" << external_modules.size() << " modules)\n";
    for (const auto* module : external_modules) {
      out_cpp << "    m_" << module->instance_name << "->eval();\n";
    }
    out_cpp << "\n";
  }

  out_cpp << "}\n";
  out_cpp << "\n";
  out_cpp << "// ============================================================================\n";
  out_cpp << "// Connection Propagation Functions\n";
  out_cpp << "// ============================================================================\n";
  out_cpp << "\n";

  // Get all connections
  std::vector<ModuleInfo> modules_list;
  for (const auto& pair : modules_) {
    modules_list.push_back(pair.second);
  }
  auto all_connections = conn_builder_->build(modules_list);

  // Separate connections by type
  std::vector<PortConnection> comb_to_seq, seq_to_comb, comb_to_ext, ext_to_comb;
  std::vector<PortConnection> top_inputs, top_outputs;

  for (const auto& conn : all_connections) {
    if (conn.is_top_level_input) {
      top_inputs.push_back(conn);
    } else if (conn.is_top_level_output) {
      top_outputs.push_back(conn);
    } else if (conn.driver_module && !conn.receiver_modules.empty()) {
      auto driver_type = conn.driver_module->type;
      auto receiver_type = conn.receiver_modules[0]->type;

      if (driver_type == ModuleType::COMB && receiver_type == ModuleType::SEQ) {
        comb_to_seq.push_back(conn);
      } else if (driver_type == ModuleType::SEQ && receiver_type == ModuleType::COMB) {
        seq_to_comb.push_back(conn);
      } else if (driver_type == ModuleType::COMB && receiver_type == ModuleType::EXTERNAL) {
        comb_to_ext.push_back(conn);
      } else if (driver_type == ModuleType::EXTERNAL && receiver_type == ModuleType::COMB) {
        ext_to_comb.push_back(conn);
      }
    }
  }

  std::sort(top_inputs.begin(), top_inputs.end(), compareTopPortNames);
  std::sort(top_outputs.begin(), top_outputs.end(), compareTopPortNames);

  // Generate port declarations for header file
  std::cout << "  Generating port declarations for header... (" << top_inputs.size()
            << " inputs, " << top_outputs.size() << " outputs)" << std::endl;
  out_h << "    // ========================================================================\n";
  out_h << "    // Top-level Input/Output Ports (Verilator-style public interface)\n";
  out_h << "    // ========================================================================\n";
  out_h << "\n";
  out_h << generate_port_declarations(top_inputs, top_outputs);
  out_h << "\n";
  out_h << "private:\n";
  out_h << "    // ========================================================================\n";
  out_h << "    // Internal Module Instances (Auto-generated)\n";
  out_h << "    // ========================================================================\n";

  // Generate member variable declarations from modules_
  for (const auto& pair : modules_) {
    const std::string& module_name = pair.first;
    const std::string& instance_name = pair.second.instance_name;
    out_h << "    V" << module_name << "* m_" << instance_name << ";\n";
  }

  out_h << "\n";
  out_h << "    // ========================================================================\n";
  out_h << "    // Connection Propagation Functions\n";
  out_h << "    // ========================================================================\n";
  out_h << "    void propagate_inputs_to_comb();\n";
  out_h << "    void propagate_comb_to_seq();\n";
  out_h << "    void propagate_seq_to_comb();\n";
  out_h << "    void propagate_comb_to_external();\n";
  out_h << "    void propagate_external_to_comb();\n";
  out_h << "    void propagate_comb_to_outputs();\n";
  out_h << "};\n";
  out_h << "\n";
  out_h << "#endif // " << header_guard << "\n";

  // Generate each propagate function
  std::cout << "  Generating propagate_inputs_to_comb()... (" << top_inputs.size() << " inputs)" << std::endl;
  out_cpp << generate_propagate_function(top_inputs, "propagate_inputs_to_comb",
                     "TOP -> COMB: " + std::to_string(top_inputs.size()) + " inputs");
  out_cpp << "\n";

  std::cout << "  Generating propagate_comb_to_seq()... (" << comb_to_seq.size() << " connections)" << std::endl;
  out_cpp << generate_propagate_function(comb_to_seq, "propagate_comb_to_seq",
                     "COMB -> SEQ: " + std::to_string(comb_to_seq.size()) + " connections");
  out_cpp << "\n";

  std::cout << "  Generating propagate_seq_to_comb()... (" << seq_to_comb.size() << " connections)" << std::endl;
  out_cpp << generate_propagate_function(seq_to_comb, "propagate_seq_to_comb",
                     "SEQ -> COMB: " + std::to_string(seq_to_comb.size()) + " connections");
  out_cpp << "\n";

  std::cout << "  Generating propagate_comb_to_external()... (" << comb_to_ext.size() << " connections)" << std::endl;
  out_cpp << generate_propagate_function(comb_to_ext, "propagate_comb_to_external",
                     "COMB -> EXTERNAL: " + std::to_string(comb_to_ext.size()) + " connections");
  out_cpp << "\n";

  std::cout << "  Generating propagate_external_to_comb()... (" << ext_to_comb.size() << " connections)" << std::endl;
  out_cpp << generate_propagate_function(ext_to_comb, "propagate_external_to_comb",
                     "EXTERNAL -> COMB: " + std::to_string(ext_to_comb.size()) + " connections");
  out_cpp << "\n";

  std::cout << "  Generating propagate_comb_to_outputs()... (" << top_outputs.size() << " outputs)" << std::endl;
  out_cpp << generate_propagate_function(top_outputs, "propagate_comb_to_outputs",
                     "COMB -> TOP: " + std::to_string(top_outputs.size()) + " outputs");
  out_cpp << "\n";

  out_cpp.close();
  out_h.close();

  // Generate Makefile
  std::string output_dir;
  size_t last_slash = output_cpp_file.rfind('/');
  if (last_slash != std::string::npos) {
    output_dir = output_cpp_file.substr(0, last_slash + 1);
  } else {
    output_dir = "./";
  }

  // Extract just the filenames (without path)
  std::string cpp_filename = output_cpp_file.substr(last_slash + 1);
  std::string h_filename = output_h_file.substr(output_h_file.rfind('/') + 1);
  std::string makefile_path = output_dir + "Makefile.wrapper";

  if (!generate_makefile(output_file_base)) {
    std::cerr << "Warning: Failed to generate Makefile" << std::endl;
  }

  std::cout << "\n=== Generation Complete ===" << std::endl;
  std::cout << "  Output CPP file: " << output_cpp_file << std::endl;
  std::cout << "  Output H file: " << output_h_file << std::endl;
  std::cout << "  Output Makefile: " << makefile_path << std::endl;
  std::cout << "  Total connections: " << all_connections.size() << std::endl;
  std::cout << "    - TOP -> COMB: " << top_inputs.size() << std::endl;
  std::cout << "    - COMB -> SEQ: " << comb_to_seq.size() << std::endl;
  std::cout << "    - SEQ -> COMB: " << seq_to_comb.size() << std::endl;
  std::cout << "    - COMB -> EXTERNAL: " << comb_to_ext.size() << std::endl;
  std::cout << "    - EXTERNAL -> COMB: " << ext_to_comb.size() << std::endl;
  std::cout << "    - COMB -> TOP: " << top_outputs.size() << std::endl;
  std::cout << "  VlWide ports: " << vlwide_ports_ << std::endl;

  return true;
}

void CodeGenerator::print_statistics() const {
  std::cout << "\n=== Code Generation Statistics ===" << std::endl;
  std::cout << "  Total connections: " << total_connections_ << std::endl;
  std::cout << "  VlWide ports: " << vlwide_ports_ << std::endl;
  std::cout << "  Top-level outputs: " << top_outputs_ << std::endl;
}

bool CodeGenerator::is_vlwide_type(const std::string& type_str) const {
  return type_str.find("VlWide<") != std::string::npos ||
         type_str.find("VlWideArray<") != std::string::npos;
}

int CodeGenerator::extract_vlwide_width(const std::string& type_str) const {
  std::regex width_regex(R"(VlWide(?:Array)?<(\d+)>)");
  std::smatch match;
  if (std::regex_search(type_str, match, width_regex)) {
    return std::stoi(match[1].str());
  }
  return 0;
}

bool CodeGenerator::generate_makefile(const std::string& output_file_base) {
  std::cout << "\n=== Generating Makefile ===" << std::endl;

  const std::string output_makefile = output_file_base + ".mk";
  const std::string wrapper_cpp_file = output_file_base + ".cpp";
  const std::string wrapper_h_file = output_file_base + ".h";

  std::ofstream out(output_makefile);
  if (!out.is_open()) {
    std::cerr << "Failed to open output Makefile: " << output_makefile << std::endl;
    return false;
  }

  out << "# Auto-generated Makefile for VCorvusTopWrapper\n";
  out << "# Generated: " << __DATE__ << " " << __TIME__ << "\n";
  out << "\n";
  out << "# Verilator configuration\n";
  out << "_CORVUS_VERILATOR_ROOT ?= $(shell verilator --getenv VERILATOR_ROOT)\n";
  out << "_CORVUS_VERILATOR_INCLUDE = $(_CORVUS_VERILATOR_ROOT)/include\n";
  out << "\n";
  out << "# Module directories\n";
  out << "_CORVUS_MODULE_PATH = " << modules_dir_ << "\n";

  // Generate module directory variables
  for (const auto& pair : modules_) {
    const std::string& module_name = pair.first;
    std::string var_name = module_name;
    std::transform(var_name.begin(), var_name.end(), var_name.begin(), ::toupper);
    std::replace(var_name.begin(), var_name.end(), '_', '_');
    out << "_CORVUS_" << var_name << "_DIR = $(_CORVUS_MODULE_PATH)/verilator-compile-" << module_name << "\n";
  }

  out << "\n";
  out << "# Compiler settings\n";
  out << "_CORVUS_CXX ?= g++\n";
  out << "_CORVUS_CXXFLAGS = -std=c++14 -Wall -Wextra -g \\\n";
  out << "                   -I. \\\n";
  out << "                   -I$(_CORVUS_VERILATOR_INCLUDE) \\\n";
  out << "                   -I$(_CORVUS_VERILATOR_INCLUDE)/vltstd";

  // Add module include paths
  for (const auto& pair : modules_) {
    const std::string& module_name = pair.first;
    std::string var_name = module_name;
    std::transform(var_name.begin(), var_name.end(), var_name.begin(), ::toupper);
    out << " \\\n                   -I$(" << "_CORVUS_" << var_name << "_DIR)";
  }
  out << "\n";

  out << "_CORVUS_LDLIBS = -lpthread -lz\n";

  // Extra include flags provided by users
  out << "\n";
  out << "# User provided extra include flags\n";
  out << "_CORVUS_USER_INCLUDE_FLAGS ?=\n";
  out << "_CORVUS_CXXFLAGS += $(_CORVUS_USER_INCLUDE_FLAGS)\n";

  // Extra macro flags provided by users
  out << "\n";
  out << "# User provided extra macro flags\n";
  out << "_CORVUS_USER_MACRO_FLAGS ?=\n";
  out << "_CORVUS_CXXFLAGS += $(_CORVUS_USER_MACRO_FLAGS)\n";

  // Extra library flags provided by users
  out << "\n";
  out << "# User provided extra library flags\n";
  out << "_CORVUS_USER_LIB_FLAGS ?=\n";
  out << "_CORVUS_LDLIBS += $(_CORVUS_USER_LIB_FLAGS)\n";

  // Verilator runtime libraries
  out << "\n";
  out << "# Verilator runtime libraries (including tracing support)\n";
  out << "_CORVUS_VERILATOR_LIBS = $(_CORVUS_VERILATOR_INCLUDE)/verilated.cpp \\\n";
  out << "                         $(_CORVUS_VERILATOR_INCLUDE)/verilated_threads.cpp \\\n";
  out << "                         $(_CORVUS_VERILATOR_INCLUDE)/verilated_fst_c.cpp\n";
  out << "\n";

  // Extra sources provided by users
  out << "\n";
  out << "# User provided extra sources\n";
  out << "_CORVUS_USER_SRC_FILES ?=\n";

  out << "\n";
  out << "# Generated module object files\n";
  out << "_CORVUS_MODULE_OBJS =";

  for (const auto& pair : modules_) {
    const std::string& module_name = pair.first;
    std::string var_name = module_name;
    std::transform(var_name.begin(), var_name.end(), var_name.begin(), ::toupper);
    out << " \\\n                      $(" << "_CORVUS_" << var_name << "_DIR)/V" << module_name << "__ALL.a";
  }

  out << "\n\n";
  out << "# Build targets\n";
  out << "# Specify your main source file when running make:\n";
  out << "#   make _CORVUS_TARGET=my_program _CORVUS_MAIN_SRC=my_main.cpp\n";
  out << "_CORVUS_TARGET ?= main\n";
  out << "_CORVUS_MAIN_SRC ?= main.cpp\n";
  out << "_CORVUS_SOURCES = $(_CORVUS_MAIN_SRC) " << wrapper_cpp_file << " $(_CORVUS_VERILATOR_LIBS) $(_CORVUS_USER_SRC_FILES)\n";
  out << "\n";
  out << ".PHONY: _CORVUS_all _CORVUS_clean _CORVUS_test\n";
  out << "\n";
  out << "_CORVUS_all: $(_CORVUS_TARGET)\n";
  out << "\n";
  out << "$(_CORVUS_TARGET): $(_CORVUS_SOURCES) " << wrapper_h_file << "\n";
  out << "\t@echo \"=== Compiling $(_CORVUS_TARGET) ===\"\n";
  out << "\t$(_CORVUS_CXX) $(_CORVUS_CXXFLAGS) -o $@ $(_CORVUS_SOURCES) $(_CORVUS_MODULE_OBJS) $(_CORVUS_LDLIBS)\n";
  out << "\n";
  out << "_CORVUS_test: $(_CORVUS_TARGET)\n";
  out << "\t@echo \"\"\n";
  out << "\t@echo \"=== Running $(_CORVUS_TARGET) ===\"\n";
  out << "\t@./$(_CORVUS_TARGET)\n";
  out << "\n";
  out << "_CORVUS_clean:\n";
  out << "\trm -f $(_CORVUS_TARGET) *.o\n";
  out << "\n";
  out << "_CORVUS_help:\n";
  out << "\t@echo \"Auto-generated Makefile for VCorvusTopWrapper\"\n";
  out << "\t@echo \"=============================================\"\n";
  out << "\t@echo \"Usage:\"\n";
  out << "\t@echo \"  make [_CORVUS_TARGET=name] [_CORVUS_MAIN_SRC=file.cpp]   - Build with custom main file\"\n";
  out << "\t@echo \"  make _CORVUS_test                                        - Build and run test program\"\n";
  out << "\t@echo \"  make _CORVUS_clean                                       - Remove build artifacts\"\n";
  out << "\t@echo \"  make _CORVUS_help                                        - Show this help message\"\n";
  out << "\t@echo \"\"\n";
  out << "\t@echo \"Examples:\"\n";
  out << "\t@echo \"  make                                                     - Build default (test_wrapper)\"\n";
  out << "\t@echo \"  make _CORVUS_TARGET=my_sim _CORVUS_MAIN_SRC=my_sim.cpp   - Build custom program\"\n";

  out.close();

  std::cout << "  Generated Makefile: " << output_makefile << std::endl;
  std::cout << "  Detected " << modules_.size() << " modules" << std::endl;

  return true;
}
