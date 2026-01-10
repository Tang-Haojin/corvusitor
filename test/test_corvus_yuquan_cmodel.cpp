#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
bool matches_key(const std::string& arg, const std::string& key) {
  return arg == key || arg.find(key + "=") == 0;
}

std::string parse_string_arg(const std::vector<std::string>& args,
                             const std::vector<std::string>& keys,
                             const std::string& default_val) {
  for (size_t i = 0; i < args.size(); ++i) {
    for (const auto& key : keys) {
      if (matches_key(args[i], key)) {
        const std::string prefix = key + "=";
        if (args[i].find(prefix) == 0) {
          return args[i].substr(prefix.size());
        }
        if (i + 1 < args.size()) {
          return args[i + 1];
        }
      }
    }
  }
  return default_val;
}

int parse_int_arg(const std::vector<std::string>& args,
                  const std::vector<std::string>& keys,
                  int default_val) {
  for (size_t i = 0; i < args.size(); ++i) {
    for (const auto& key : keys) {
      if (matches_key(args[i], key)) {
        const std::string prefix = key + "=";
        try {
          if (args[i].find(prefix) == 0) {
            return std::stoi(args[i].substr(prefix.size()));
          }
          if (i + 1 < args.size()) {
            return std::stoi(args[i + 1]);
          }
        } catch (...) {
          return default_val;
        }
      }
    }
  }
  return default_val;
}
} // namespace

// Integration test that runs cmodel corvusitor generation against a YuQuan sample build.
int main(int argc, char* argv[]) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const int mbus_count = parse_int_arg(args, {std::string("--mbus-count")}, 8);
  const int sbus_count = parse_int_arg(args, {std::string("--sbus-count")}, 8);
  const std::string modules_dir = parse_string_arg(
      args, {std::string("--module-build-dir"), std::string("--modules-dir")}, "test/YuQuan/build/sim");
  const std::string output_base = parse_string_arg(
      args, {std::string("--output-base")}, "build/yuquan");
  const std::string corvusitor_bin = parse_string_arg(
      args, {std::string("--corvusitor-bin")}, "./build/corvusitor");

  // Clean stale artifacts to ensure we validate the new generation.
  std::remove((output_base + "_corvus.json").c_str());
  std::remove((output_base + "_corvus_gen.h").c_str());
  std::remove((output_base + "_corvus_cmodel_gen.h").c_str());

  std::ostringstream cmd;
  cmd << corvusitor_bin
      << " --module-build-dir " << modules_dir
      << " --output-name " << output_base
      << " --mbus-count " << mbus_count
      << " --sbus-count " << sbus_count
      << " --target cmodel";
  const int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    std::cerr << "corvusitor cmodel generation failed with code " << ret << "\n";
    return 1;
  }

  std::ifstream json(output_base + "_corvus.json");
  if (!json.is_open()) {
    std::cerr << "Missing YuQuan corvus json\n";
    return 1;
  }

  std::ifstream header(output_base + "_corvus_gen.h");
  if (!header.is_open()) {
    std::cerr << "Missing YuQuan generated header (corvus)\n";
    return 1;
  }

  std::ifstream cmodel_header(output_base + "_corvus_cmodel_gen.h");
  if (!cmodel_header.is_open()) {
    std::cerr << "Missing YuQuan cmodel generated header\n";
    return 1;
  }
  std::string cmodel_content((std::istreambuf_iterator<char>(cmodel_header)),
                             std::istreambuf_iterator<char>());
  if (cmodel_content.find("CorvusCModelGen") == std::string::npos ||
      cmodel_content.find("CorvusTopModuleGen") == std::string::npos ||
      cmodel_content.find("CorvusSimWorkerGenP0") == std::string::npos) {
    std::cerr << "CModel header missing expected symbols\n";
    return 1;
  }

  std::cout << "corvus_yuquan_cmodel: PASS\n";
  return 0;
}
