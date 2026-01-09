#include "../include/code_generator.h"
#include <fstream>
#include <iostream>
#include <string>

namespace {
int parse_arg(const std::vector<std::string>& args, const std::string& key, int default_val) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) {
      try {
        return std::stoi(args[i + 1]);
      } catch (...) {
        return default_val;
      }
    }
  }
  return default_val;
}
} // namespace

// Integration test that runs codegen against the YuQuan verilator outputs.
int main(int argc, char* argv[]) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const int mbus_count = parse_arg(args, std::string("--mbus-count"), 8);
  const int sbus_count = parse_arg(args, std::string("--sbus-count"), 8);
  const std::string modules_dir = "test/YuQuan/build/sim";
  const std::string output_base = "build/yuquan_corvus_codegen";

  CodeGenerator gen(modules_dir, mbus_count, sbus_count);
  if (!gen.load_data()) {
    std::cerr << "Failed to load YuQuan modules\n";
    return 1;
  }

  if (!gen.generate_all(output_base)) {
    std::cerr << "Failed to generate YuQuan corvus artifacts\n";
    return 1;
  }

  std::ifstream json(output_base + "_corvus.json");
  if (!json.is_open()) {
    std::cerr << "Missing YuQuan corvus json\n";
    return 1;
  }
  std::string json_content((std::istreambuf_iterator<char>(json)),
                           std::istreambuf_iterator<char>());
  if (json_content.find("partitions") == std::string::npos ||
      json_content.find("top_inputs") == std::string::npos) {
    std::cerr << "YuQuan json lacks expected sections\n";
    return 1;
  }

  std::ifstream header(output_base + "_corvus_gen.h");
  if (!header.is_open()) {
    std::cerr << "Missing YuQuan generated header\n";
    return 1;
  }
  std::string header_content((std::istreambuf_iterator<char>(header)),
                             std::istreambuf_iterator<char>());
  if (header_content.find("CorvusTopModuleGen") == std::string::npos ||
      header_content.find("CorvusSimWorkerGenP0") == std::string::npos) {
    std::cerr << "Generated header missing expected classes\n";
    return 1;
  }

  std::cout << "corvus_yuquan: PASS\n";
  return 0;
}
