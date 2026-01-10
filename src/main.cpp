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
#include <algorithm>
#include <iostream>
#include <string>

namespace {

char path_separator() {
#ifdef _WIN32
  return '\\';
#else
  return '/';
#endif
}

std::string join_path(const std::string& dir, const std::string& base) {
  if (dir.empty() || dir == ".") return base;
  char sep = path_separator();
  if (dir.back() == '/' || dir.back() == '\\') {
    return dir + base;
  }
  return dir + sep + base;
}

std::string sanitize_identifier(const std::string& base) {
  std::string id;
  id.reserve(base.size());
  for (char c : base) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      id.push_back(c);
    } else {
      id.push_back('_');
    }
  }
  if (id.empty()) id = "output";
  if (std::isdigit(static_cast<unsigned char>(id.front()))) {
    id.insert(id.begin(), '_');
  }
  return id;
}

std::string output_token(const std::string& output_base) {
  std::string name = output_base;
  size_t pos = name.find_last_of("/\\");
  if (pos != std::string::npos) name = name.substr(pos + 1);
  return sanitize_identifier(name);
}

std::string class_prefix(const std::string& output_base) {
  return "C" + output_token(output_base);
}

} // namespace

int main(int argc, char* argv[]) {
  cxxopts::Options options("Corvusitor", std::string(argv[0]) + ": Generate a wrapper for compiled corvus-compiler output");
  options.add_options()
    ("m,modules-dir", "Path to the modules directory", cxxopts::value<std::string>()->default_value("."))
    ("module-build-dir", "Path to a specific module build directory (overrides modules-dir)", cxxopts::value<std::string>())
    ("output-dir", "Directory for generated artifacts", cxxopts::value<std::string>()->default_value("."))
    ("o,output-name", "Output name (file prefix for corvus artifacts)", cxxopts::value<std::string>()->default_value("corvus_codegen"))
    ("mbus-count", "Number of MBus endpoints to target (compile-time routing)", cxxopts::value<int>()->default_value("8"))
    ("sbus-count", "Number of SBus endpoints to target (compile-time routing)", cxxopts::value<int>()->default_value("8"))
    ("target", "Generation target: corvus (default) or cmodel", cxxopts::value<std::string>()->default_value("corvus"))
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
  if (result.count("module-build-dir")) {
    modules_dir = result["module-build-dir"].as<std::string>();
  }
  std::cout << "\nModule directory: " << modules_dir << "\n";
  int mbus_count = result["mbus-count"].as<int>();
  int sbus_count = result["sbus-count"].as<int>();
  std::string target_str = result["target"].as<std::string>();
  std::string target_lower = target_str;
  std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), ::tolower);
  CodeGenerator::GenerationTarget target = CodeGenerator::GenerationTarget::Corvus;
  if (target_lower == "cmodel") {
    target = CodeGenerator::GenerationTarget::CorvusCModel;
  } else if (target_lower != "corvus") {
    std::cerr << "Unknown target: " << target_str << " (expected corvus or cmodel)\n";
    return 1;
  }

  // Create code generator
  CodeGenerator generator(modules_dir, mbus_count, sbus_count, target);

  // Load module and connection data
  if (!generator.load_data()) {
    std::cerr << "\nError: Failed to load module data\n";
    return 1;
  }

  // Generate corvus artifacts
  std::string output_dir = result["output-dir"].as<std::string>();
  std::string output_name = result["output-name"].as<std::string>();
  std::string output_base = join_path(output_dir, output_name);
  std::cout << "\nOutput directory: " << output_dir << "\n";
  std::cout << "Output name: " << output_name << "\n";
  std::cout << "Output base: " << output_base << "\n";
  std::string prefix = class_prefix(output_base);

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
  std::cout << "  - " << join_path(output_dir, prefix + "CorvusGen.h") << " (includes generated headers)\n";
  std::cout << "  - " << join_path(output_dir, prefix + "TopModuleGen.h") << " / "
            << join_path(output_dir, prefix + "TopModuleGen.cpp") << "\n";
  std::cout << "  - " << join_path(output_dir, prefix + "SimWorkerGenP*.h") << " / "
            << join_path(output_dir, prefix + "SimWorkerGenP*.cpp") << "\n";
  if (target == CodeGenerator::GenerationTarget::CorvusCModel) {
    std::cout << "  - " << join_path(output_dir, prefix + "CModelGen.h") << "\n";
  }
  std::cout << "\nNext steps:\n";
  std::cout << "  1) Inspect the JSON to feed downstream corvus codegen\n";
  std::cout << "  2) Include the generated header to instantiate corvus top/worker classes\n";
  if (target == CodeGenerator::GenerationTarget::CorvusCModel) {
    std::cout << "  3) Include the cmodel header to get runnable top/worker orchestration\n";
  }
  std::cout << "\n";

  return 0;
}
