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
    ("o,output-name", "Output base name (corvus artifacts)", cxxopts::value<std::string>()->default_value("corvus_codegen"))
    ("mbus-count", "Number of MBus endpoints to target (compile-time routing)", cxxopts::value<int>()->default_value("8"))
    ("sbus-count", "Number of SBus endpoints to target (compile-time routing)", cxxopts::value<int>()->default_value("8"))
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
  int mbus_count = result["mbus-count"].as<int>();
  int sbus_count = result["sbus-count"].as<int>();

  // Create code generator
  CodeGenerator generator(modules_dir, mbus_count, sbus_count);

  // Load module and connection data
  if (!generator.load_data()) {
    std::cerr << "\nError: Failed to load module data\n";
    return 1;
  }

  // Generate corvus artifacts
  std::string output_base = result["output-name"].as<std::string>();
  std::cout << "\nOutput base: " << output_base << "\n";

  if (!generator.generate_all(output_base)) {
    std::cerr << "\nError: Failed to generate code\n";
    return 1;
  }

  // Print statistics
  generator.print_statistics();

  std::cout << "\n====================================================\n";
  std::cout << "  Generation Successful!\n";
  std::cout << "====================================================\n";
  std::cout << "\nArtifacts:\n";
  std::cout << "  - " << output_base << "_corvus.json\n";
  std::cout << "  - " << output_base << "_corvus_gen.h\n";
  std::cout << "\nNext steps:\n";
  std::cout << "  1) Inspect the JSON to feed downstream corvus codegen\n";
  std::cout << "  2) Include the generated header to instantiate corvus top/worker classes\n";
  std::cout << "\n";

  return 0;
}
