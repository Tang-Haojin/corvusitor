/**
 * test_code_generator.cpp - Code Generator Test
 *
 * Features:
 * 1. Load YuQuan 5 module information
 * 2. Build connection relationships
 * 3. Generate all propagate function implementations
 * 4. Output statistics
 */

#include "code_generator.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  std::cout << "====================================================\n";
  std::cout << "  YuQuan Code Generator Test\n";
  std::cout << "====================================================\n";

  // Module directory (from external-project)
  std::string modules_dir = "../external-project/YuQuan/build/sim";
  if (argc > 1) {
    modules_dir = argv[1];
  }

  std::cout << "\nModule directory: " << modules_dir << "\n";

  // Create code generator
  CodeGenerator generator(modules_dir);

  // Load module and connection data
  if (!generator.load_data()) {
    std::cerr << "\nError: Failed to load module data\n";
    return 1;
  }

  // Generate all connection code (CPP + H)
  std::string output_cpp = "../phase3-yuquan/VCorvusTopWrapper_generated.cpp";
  std::string output_base = output_cpp.substr(0, output_cpp.rfind(".cpp"));
  std::string output_h = "../phase3-yuquan/VCorvusTopWrapper_generated.h";
  if (argc > 2) {
    output_cpp = argv[2];
    output_h = std::string(argv[2]).substr(0, std::string(argv[2]).rfind(".cpp")) + ".h";
  }

  std::cout << "\nOutput CPP file: " << output_cpp << "\n";
  std::cout << "Output H file: " << output_h << "\n";

  if (!generator.generate_all(output_base)) {
    std::cerr << "\nError: Failed to generate code\n";
    return 1;
  }

  // Print statistics
  generator.print_statistics();

  std::cout << "\n====================================================\n";
  std::cout << "  Generation Successful!\n";
  std::cout << "====================================================\n";
  std::cout << "\nNext steps:\n";
  std::cout << "  1. Review generated files:\n";
  std::cout << "     - " << output_cpp << "\n";
  std::cout << "     - " << output_h << "\n";
  std::cout << "  2. Insert " << output_h << " content into VCorvusTopWrapper.h public section\n";
  std::cout << "  3. Replace VCorvusTopWrapper.cpp propagate functions with generated code\n";
  std::cout << "  4. Recompile with: make -f Makefile.wrapper\n";
  std::cout << "\n";

  return 0;
}
