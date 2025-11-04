#include "../include/module_parser.h"
#include <iostream>
#include <vector>

int main() {
  ModuleParser parser;

  // Test parsing header files in example directory
  std::vector<std::string> test_files = {
    "example/verilator-compile-corvus_comb_P0/Vcorvus_comb_P0.h",
    "example/verilator-compile-corvus_comb_P1/Vcorvus_comb_P1.h",
    "example/verilator-compile-corvus_seq_P0/Vcorvus_seq_P0.h",
    "example/verilator-compile-corvus_seq_P1/Vcorvus_seq_P1.h",
    "example/verilator-compile-corvus_external/Vcorvus_external.h"
  };

  std::cout << "=== Module Parser Test ===" << std::endl << std::endl;

  for (const auto& file : test_files) {
    std::cout << "Parsing: " << file << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    try {
      ModuleInfo info = parser.parse(file);

      std::cout << "  Module Name:    " << info.module_name << std::endl;
      std::cout << "  Class Name:     " << info.class_name << std::endl;
      std::cout << "  Instance Name:  " << info.instance_name << std::endl;
      std::cout << "  Type:           " << info.get_type_str() << std::endl;
      std::cout << "  Partition ID:   " << info.partition_id << std::endl;
      std::cout << "  Library Path:   " << info.lib_path << std::endl;
      std::cout << "  Total Ports:    " << info.ports.size() << std::endl;

      auto inputs = info.get_inputs();
      auto outputs = info.get_outputs();
      std::cout << "  Input Ports:    " << inputs.size() << std::endl;
      std::cout << "  Output Ports:   " << outputs.size() << std::endl;

      // Show first 5 input ports
      std::cout << "\n  Sample Input Ports:" << std::endl;
      for (size_t i = 0; i < std::min(5ul, inputs.size()); i++) {
        const auto& port = inputs[i];
        std::cout << "    [" << i << "] "
                  << port.get_cpp_type() << " "
                  << port.name
                  << " [" << port.msb << ":" << port.lsb << "]"
                  << " (width=" << port.get_width() << ")"
                  << std::endl;
      }

      // Show first 5 output ports
      std::cout << "\n  Sample Output Ports:" << std::endl;
      for (size_t i = 0; i < std::min(5ul, outputs.size()); i++) {
        const auto& port = outputs[i];
        std::cout << "    [" << i << "] "
                  << port.get_cpp_type() << " "
                  << port.name
                  << " [" << port.msb << ":" << port.lsb << "]"
                  << " (width=" << port.get_width() << ")"
                  << std::endl;
      }

      std::cout << std::endl;

    } catch (const std::exception& e) {
      std::cerr << "  ERROR: " << e.what() << std::endl << std::endl;
    }
  }

  std::cout << "=== Test Complete ===" << std::endl;

  return 0;
}
