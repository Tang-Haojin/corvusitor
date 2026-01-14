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

bool is_absolute(const std::string& path) {
  return !path.empty() && (path.front() == '/' || (path.size() > 1 && path[1] == ':'));
}

std::string resolve_path(const std::string& base, const std::string& path) {
  if (path.empty() || is_absolute(path)) return path;
  return join_path(base, path);
}
} // namespace

// Integration test that runs cmodel corvusitor generation against a YuQuan sample build.
int main(int argc, char* argv[]) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const int mbus_count = parse_int_arg(args, {std::string("--mbus-count")}, 8);
  const int sbus_count = parse_int_arg(args, {std::string("--sbus-count")}, 3);
  const char* pwd_env = std::getenv("PWD");
  const std::string repo_root = pwd_env ? pwd_env : ".";
  const std::string yuquan_dir = resolve_path(
      repo_root, parse_string_arg(args, {std::string("--yuquan-dir")}, "test/YuQuan"));
  const std::string modules_dir = resolve_path(
      repo_root, parse_string_arg(args, {std::string("--module-build-dir"), std::string("--modules-dir")}, "test/YuQuan/build/sim"));
  const std::string output_dir = resolve_path(
      repo_root, parse_string_arg(args, {std::string("--output-dir")}, "test/YuQuan/build"));
  const std::string output_name = parse_string_arg(
      args, {std::string("--output-name")}, "YuQuan");
  const std::string output_base = join_path(output_dir, output_name);
  const std::string corvusitor_bin = resolve_path(
      repo_root, parse_string_arg(args, {std::string("--corvusitor-bin")}, "./build/corvusitor"));

  // Clean stale artifacts to ensure we validate the new generation.
  std::remove((output_base + "_connection_analysis.json").c_str());
  std::remove((output_base + "_corvus_bus_plan.json").c_str());
  const std::string prefix = class_prefix(output_base);
  std::remove(join_path(output_dir, prefix + "CorvusGen.h").c_str());
  std::remove(join_path(output_dir, prefix + "CModelGen.h").c_str());

  std::ostringstream cmd;
  cmd << "make -C " << yuquan_dir << " yuquan_cmodel_gen"
      << " CORVUSITOR_BIN=" << corvusitor_bin
      << " CORVUSITOR_MBUS_COUNT=" << mbus_count
      << " CORVUSITOR_SBUS_COUNT=" << sbus_count
      << " YUQUAN_SIM_DIR=" << modules_dir
      << " CORVUSITOR_OUTPUT_DIR=" << output_dir
      << " CORVUSITOR_CMODEL_OUTPUT_NAME=" << output_name;
  const int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    std::cerr << "yuquan_cmodel_gen failed with code " << ret << "\n";
    return 1;
  }

  std::ifstream json(output_base + "_corvus_bus_plan.json");
  if (!json.is_open()) {
    std::cerr << "Missing YuQuan corvus json\n";
    return 1;
  }

  std::ifstream header(join_path(output_dir, prefix + "CorvusGen.h"));
  if (!header.is_open()) {
    std::cerr << "Missing YuQuan generated header (corvus)\n";
    return 1;
  }

  std::ifstream cmodel_header(join_path(output_dir, prefix + "CModelGen.h"));
  if (!cmodel_header.is_open()) {
    std::cerr << "Missing YuQuan cmodel generated header\n";
    return 1;
  }
  std::string cmodel_content((std::istreambuf_iterator<char>(cmodel_header)),
                             std::istreambuf_iterator<char>());
  if (cmodel_content.find(prefix + "CModelGen") == std::string::npos ||
      cmodel_content.find(prefix + "TopModuleGen") == std::string::npos ||
      cmodel_content.find(prefix + "SimWorkerGenP0") == std::string::npos) {
    std::cerr << "CModel header missing expected symbols\n";
    return 1;
  }

  std::cout << "corvus_yuquan_cmodel: PASS\n";
  return 0;
}
