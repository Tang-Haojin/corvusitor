/**
 * Code Generator
 *
 * Features:
 * 1. Load module information
 * 2. Build connection relationships
 * 3. Generate all propagate function implementations
 * 4. Output statistics
 */

#include "code_generator.h"
#include "cxxopts.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  cxxopts::Options options("Corvusitor", std::string(argv[0]) + ": Generate a wrapper for compiled corvus-compiler output");
  options.add_options()
    ("m,modules-dir", "Path to the modules directory", cxxopts::value<std::string>()->default_value("."))
    ("o,output-name", "Output C++ implementation file name", cxxopts::value<std::string>()->default_value("VCorvusTopWrapper_generated.cpp"))
    ("h,help", "Print usage")
    ;
  auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    exit(0);
  }

  std::cout << "====================================================\n";
  std::cout << "  Corvusitor\n";
  std::cout << "====================================================\n";

  // Module directory
  std::string modules_dir = result["modules-dir"].as<std::string>();
  std::cout << "\nModule directory: " << modules_dir << "\n";

  // Create code generator
  CodeGenerator generator(modules_dir);

  // Load module and connection data
  if (!generator.load_data()) {
    std::cerr << "\nError: Failed to load module data\n";
    return 1;
  }

  // Generate all connection code (CPP + H)
  std::string output_cpp = result["output-name"].as<std::string>();
  std::string output_base = output_cpp.substr(0, output_cpp.rfind(".cpp"));
  std::string output_h = output_base + ".h";
  std::string output_mk = output_base + ".mk";

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
  std::cout << "     - " << output_mk << "\n";

  // Extract output directory
  std::string output_dir = output_cpp.substr(0, output_cpp.rfind('/') + 1);
  std::cout << "\n  2. Create your main source file (e.g., test_wrapper.cpp) in:\n";
  std::cout << "     " << output_dir << "\n";
  std::cout << "\n  3. Build with auto-generated Makefile:\n";
  std::cout << "     cd " << output_dir << "\n";
  std::cout << "     make -f " << output_mk << "\n";
  std::cout << "\n  4. Or build with custom main file:\n";
  std::cout << "     make -f " << output_mk << " TARGET=my_sim MAIN_SRC=my_sim.cpp\n";
  std::cout << "\n  5. Run 'make -f " << output_mk << " help' for more options\n";
  std::cout << "\n";

  return 0;
}
