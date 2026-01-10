#include <cstdio>
#include <cstdlib>
#include <cctype>
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

std::string class_prefix(const std::string& output_base) {
  std::string token = output_base;
  size_t pos = token.find_last_of("/\\");
  if (pos != std::string::npos) token = token.substr(pos + 1);
  for (char& c : token) {
    if (!std::isalnum(static_cast<unsigned char>(c))) {
      c = '_';
    }
  }
  if (token.empty()) token = "output";
  if (std::isdigit(static_cast<unsigned char>(token.front()))) {
    token.insert(token.begin(), '_');
  }
  return "C" + token;
}

std::string join_path(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir == ".") return name;
  if (dir.back() == '/' || dir.back() == '\\') {
    return dir + name;
  }
  return dir + "/" + name;
}
} // namespace

// Integration test that runs corvusitor against a YuQuan build as a sample modules directory.
int main(int argc, char* argv[]) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const int mbus_count = parse_int_arg(args, {std::string("--mbus-count")}, 8);
  const int sbus_count = parse_int_arg(args, {std::string("--sbus-count")}, 8);
  const std::string modules_dir = parse_string_arg(
      args, {std::string("--module-build-dir"), std::string("--modules-dir")}, "test/YuQuan/build/sim");
  const std::string output_dir = parse_string_arg(
      args, {std::string("--output-dir")}, "build");
  const std::string output_name = parse_string_arg(
      args, {std::string("--output-name")}, "yuquan_corvus_codegen");
  const std::string output_base = join_path(output_dir, output_name);
  const std::string corvusitor_bin = parse_string_arg(
      args, {std::string("--corvusitor-bin")}, "./build/corvusitor");

  // Clean stale artifacts to ensure we validate the new generation.
  std::remove((output_base + "_corvus.json").c_str());
  std::remove(join_path(output_dir, class_prefix(output_base) + "CorvusGen.h").c_str());

  std::ostringstream cmd;
  cmd << corvusitor_bin
      << " --module-build-dir " << modules_dir
      << " --output-dir " << output_dir
      << " --output-name " << output_name
      << " --mbus-count " << mbus_count
      << " --sbus-count " << sbus_count;
  const int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    std::cerr << "corvusitor generation failed with code " << ret << "\n";
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

  const std::string prefix = class_prefix(output_base);
  std::ifstream top_header(join_path(output_dir, prefix + "TopModuleGen.h"));
  std::ifstream worker_header(join_path(output_dir, prefix + "SimWorkerGenP0.h"));
  if (!top_header.is_open() || !worker_header.is_open()) {
    std::cerr << "Missing YuQuan generated headers\n";
    return 1;
  }
  std::string top_content((std::istreambuf_iterator<char>(top_header)),
                          std::istreambuf_iterator<char>());
  std::string worker_content((std::istreambuf_iterator<char>(worker_header)),
                             std::istreambuf_iterator<char>());
  if (top_content.find(prefix + "TopModuleGen") == std::string::npos ||
      worker_content.find(prefix + "SimWorkerGenP0") == std::string::npos) {
    std::cerr << "Generated header missing expected classes\n";
    return 1;
  }

  std::cout << "corvus_yuquan: PASS\n";
  return 0;
}
