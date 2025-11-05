#include "../include/module_parser.h"
#include "../include/connection_builder.h"
#include <iostream>
#include <vector>

int main() {
  // Use Verilator parser for testing
  VerilatorModuleParser parser;
  ConnectionBuilder builder;

  std::vector<std::string> test_files = {
    "example/verilator-compile-corvus_comb_P0/Vcorvus_comb_P0.h",
    "example/verilator-compile-corvus_comb_P1/Vcorvus_comb_P1.h",
    "example/verilator-compile-corvus_seq_P0/Vcorvus_seq_P0.h",
    "example/verilator-compile-corvus_seq_P1/Vcorvus_seq_P1.h",
    "example/verilator-compile-corvus_external/Vcorvus_external.h"
  };

  std::cout << "=== Connection Builder Test ===" << std::endl << std::endl;

  // Step 1: Parse all modules
  std::cout << "Step 1: Parsing modules..." << std::endl;
  std::vector<ModuleInfo> modules;

  for (const auto& file : test_files) {
    try {
      ModuleInfo info = parser.parse(file);
      std::cout << "  Parsed " << info.module_name
           << " (" << info.ports.size() << " ports)" << std::endl;
      modules.push_back(info);
    } catch (const std::exception& e) {
      std::cerr << "  ERROR parsing " << file << ": " << e.what() << std::endl;
      return 1;
    }
  }

  std::cout << std::endl;

  // Step 2: Build connections
  std::cout << "Step 2: Building connections..." << std::endl;
  std::vector<PortConnection> connections = builder.build(modules);

  std::cout << "Total connections: " << connections.size() << std::endl << std::endl;

  // Step 3: Statistics analysis
  std::cout << "Step 3: Connection Analysis" << std::endl;
  std::cout << std::string(60, '-') << std::endl;

  int internal_count = 0;
  int top_input_count = 0;
  int top_output_count = 0;

  for (const auto& conn : connections) {
    if (conn.is_top_level_input) {
      top_input_count++;
    } else if (conn.is_top_level_output) {
      top_output_count++;
    } else {
      internal_count++;
    }
  }

  std::cout << "  Internal connections:  " << internal_count << std::endl;
  std::cout << "  Top-level inputs:      " << top_input_count << std::endl;
  std::cout << "  Top-level outputs:     " << top_output_count << std::endl;
  std::cout << std::endl;

  // Step 4: Show sample connections
  std::cout << "Step 4: Sample Connections" << std::endl;
  std::cout << std::string(60, '-') << std::endl;

  int internal_shown = 0;
  int top_input_shown = 0;
  int top_output_shown = 0;

  for (const auto& conn : connections) {
    if (internal_shown < 5 && !conn.is_top_level_input && !conn.is_top_level_output) {
      std::cout << "\n[Internal] " << conn.port_name
           << " (width=" << conn.width << ")" << std::endl;
      std::cout << "  Driver:    " << conn.driver_module->instance_name << std::endl;
      std::cout << "  Receivers: ";
      for (size_t i = 0; i < conn.receiver_modules.size(); i++) {
        std::cout << conn.receiver_modules[i]->instance_name;
        if (i < conn.receiver_modules.size() - 1) std::cout << ", ";
      }
      std::cout << std::endl;
      internal_shown++;
    }

    if (top_input_shown < 3 && conn.is_top_level_input) {
      std::cout << "\n[Top Input] " << conn.port_name
           << " (width=" << conn.width << ")" << std::endl;
      std::cout << "  Receivers: ";
      for (size_t i = 0; i < conn.receiver_modules.size(); i++) {
        std::cout << conn.receiver_modules[i]->instance_name;
        if (i < conn.receiver_modules.size() - 1) std::cout << ", ";
      }
      std::cout << std::endl;
      top_input_shown++;
    }

    if (top_output_shown < 3 && conn.is_top_level_output) {
      std::cout << "\n[Top Output] " << conn.port_name
           << " (width=" << conn.width << ")" << std::endl;
      std::cout << "  Driver:    " << conn.driver_module->instance_name << std::endl;
      top_output_shown++;
    }
  }

  std::cout << "\n\n=== Test Complete ===" << std::endl;

  return 0;
}
