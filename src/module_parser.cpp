#include "module_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

// ============================================================================
// ModuleParser Base Class - Common utility functions
// ============================================================================

ModuleType ModuleParser::infer_module_type(const std::string& module_name) {
  if (module_name.find("_comb_") != std::string::npos) {
    return ModuleType::COMB;
  } else if (module_name.find("_seq_") != std::string::npos) {
    return ModuleType::SEQ;
  } else if (module_name.find("_external") != std::string::npos) {
    return ModuleType::EXTERNAL;
  }
  throw std::runtime_error("Unknown module type: " + module_name);
}

int ModuleParser::extract_partition_id(const std::string& module_name) {
  // Find the number after _P
  size_t p_pos = module_name.find("_P");
  if (p_pos == std::string::npos) {
    return -1;  // external has no partition ID
  }

  // Extract the number after P
  std::string id_str;
  for (size_t i = p_pos + 2; i < module_name.length(); i++) {
    if (std::isdigit(module_name[i])) {
      id_str += module_name[i];
    } else {
      break;
    }
  }

  if (id_str.empty()) {
    return -1;
  }

  return std::stoi(id_str);
}

std::string ModuleParser::generate_instance_name(const std::string& class_name, ModuleType /* type */) {
  // "Vcorvus_comb_P0" -> "comb_p0"
  // "Vcorvus_seq_P1" -> "seq_p1"
  // "Vcorvus_external" -> "external"

  std::string name = class_name;

  // Remove V prefix
  if (name[0] == 'V') {
    name = name.substr(1);
  }

  // Remove corvus_ prefix
  size_t corvus_pos = name.find("corvus_");
  if (corvus_pos == 0) {
    name = name.substr(7);  // "corvus_" length is 7
  }

  // Convert to lowercase and handle P -> p
  for (size_t i = 0; i < name.length(); i++) {
    if (name[i] == 'P' && i + 1 < name.length() && std::isdigit(name[i + 1])) {
      name[i] = 'p';
    } else {
      name[i] = std::tolower(name[i]);
    }
  }

  return name;
}

// ============================================================================
// VerilatorModuleParser Implementation
// ============================================================================

VerilatorModuleParser::VerilatorModuleParser() {
  // Match VL_IN8(&signal_name, 7, 0); or VL_OUT64(&signal_name, 63, 0);
  // Note: 17-32 bits use VL_IN/VL_OUT (no suffix), not VL_IN32/VL_OUT32
  // Note: 65+ bits use VL_INW/OUTW with 4 arguments: (&name, msb, lsb, words)
  // 3-argument format: VL_(IN|OUT)(8|16|64|)(&name, msb, lsb)
  port_macro_regex = std::regex(
    R"(VL_(IN|OUT)(8|16|64|)\s*\(\s*&([^,\)]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))"
  );
  // 4-argument format: VL_(IN|OUT)W(&name, msb, lsb, words)
  port_macro_regex_wide = std::regex(
    R"(VL_(IN|OUT)W\s*\(\s*&([^,\)]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))"
  );
}

ModuleInfo VerilatorModuleParser::parse(const std::string& header_path) {
  ModuleInfo info;
  info.header_path = header_path;

  // Extract module name from file name
  // "/path/to/Vcorvus_comb_P0.h" -> "Vcorvus_comb_P0"
  size_t last_slash = header_path.find_last_of('/');
  size_t dot_pos = header_path.find_last_of('.');

  if (last_slash == std::string::npos) {
    last_slash = 0;
  } else {
    last_slash++;
  }

  if (dot_pos == std::string::npos || dot_pos < last_slash) {
    throw std::runtime_error("Invalid header file path: " + header_path);
  }

  info.class_name = header_path.substr(last_slash, dot_pos - last_slash);

  // Remove 'V' prefix to get module name
  if (info.class_name[0] == 'V') {
    info.module_name = info.class_name.substr(1);
  } else {
    info.module_name = info.class_name;
  }

  // Infer module type and partition ID
  info.type = infer_module_type(info.module_name);
  info.partition_id = extract_partition_id(info.module_name);
  info.instance_name = generate_instance_name(info.class_name, info.type);
  info.lib_path = infer_lib_path(header_path, info.class_name);

  // Read file and parse ports
  std::ifstream file(header_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open: " + header_path);
  }

  std::string line;
  int line_num = 0;
  while (std::getline(file, line)) {
    line_num++;
    // Find VL_IN/VL_OUT macro
    if (line.find("VL_IN") != std::string::npos ||
      line.find("VL_OUT") != std::string::npos) {
      try {
        PortInfo port = parse_port_macro(line);
        info.ports.push_back(port);
      } catch (const std::exception& e) {
        // Parse failure is not fatal, just a warning
        std::cerr << "Warning at line " << line_num << ": " << e.what() << std::endl;
      }
    }
  }

  std::cout << "Parsed " << info.ports.size() << " ports from " << info.class_name << std::endl;

  return info;
}

PortInfo VerilatorModuleParser::parse_port_macro(const std::string& line) {
  std::smatch match;
  PortInfo port;

  // Try matching 4-argument format (VL_INW/OUTW)
  if (std::regex_search(line, match, port_macro_regex_wide)) {
    // Direction: IN or OUT
    port.direction = (match[1] == "IN") ? PortDirection::INPUT : PortDirection::OUTPUT;

    // Bit width type: W (65+ bits)
    port.width_type = PortWidthType::VL_W;

    // Port name
    port.name = match[2];
    port.name.erase(0, port.name.find_first_not_of(" \t"));
    port.name.erase(port.name.find_last_not_of(" \t") + 1);

    // msb, lsb, words
    port.msb = std::stoi(match[3]);
    port.lsb = std::stoi(match[4]);
    port.array_size = std::stoi(match[5]);  // words 参数

    return port;
  }

  // Then try matching 3-argument format (VL_IN8/16/64/etc)
  if (std::regex_search(line, match, port_macro_regex)) {
    // Direction: IN or OUT
    port.direction = (match[1] == "IN") ? PortDirection::INPUT : PortDirection::OUTPUT;

    // Bit width type: 8, 16, "", 64
    // Note: empty string "" means 17-32 bits (VL_IN/VL_OUT no suffix)
    std::string width_str = match[2];
    if (width_str == "8")       port.width_type = PortWidthType::VL_8;
    else if (width_str == "16") port.width_type = PortWidthType::VL_16;
    else if (width_str == "")   port.width_type = PortWidthType::VL_32;  // VL_IN/VL_OUT (17-32位)
    else if (width_str == "64") port.width_type = PortWidthType::VL_64;
    else throw std::runtime_error("Unknown width type: " + width_str);

    // Port name
    port.name = match[3];
    port.name.erase(0, port.name.find_first_not_of(" \t"));
    port.name.erase(port.name.find_last_not_of(" \t") + 1);

    // msb, lsb
    port.msb = std::stoi(match[4]);
    port.lsb = std::stoi(match[5]);
    port.array_size = 0;  // Not an array type

    return port;
  }

  throw std::runtime_error("Invalid port macro format");
}

std::string VerilatorModuleParser::infer_lib_path(const std::string& header_path, const std::string& class_name) {
  // "/path/to/verilator-compile-xxx/Vxxx.h"
  // -> "/path/to/verilator-compile-xxx/libVxxx.a"

  size_t last_slash = header_path.find_last_of('/');
  std::string dir;

  if (last_slash != std::string::npos) {
    dir = header_path.substr(0, last_slash + 1);
  } else {
    dir = "./";
  }

  return dir + "lib" + class_name + ".a";
}

// ============================================================================
// VCSModuleParser Implementation (Placeholder)
// ============================================================================

VCSModuleParser::VCSModuleParser() {
  // TODO: Initialize VCS-specific regex patterns
}

ModuleInfo VCSModuleParser::parse(const std::string& header_path) {
  // TODO: Implement VCS-specific parsing logic
  std::cerr << "VCS parser not yet implemented for: " << header_path << std::endl;
  throw std::runtime_error("VCS parser not yet implemented");
}

// ============================================================================
// ModelsimModuleParser Implementation (Placeholder)
// ============================================================================

ModelsimModuleParser::ModelsimModuleParser() {
  // TODO: Initialize Modelsim-specific regex patterns
}

ModuleInfo ModelsimModuleParser::parse(const std::string& header_path) {
  // TODO: Implement Modelsim-specific parsing logic
  std::cerr << "Modelsim parser not yet implemented for: " << header_path << std::endl;
  throw std::runtime_error("Modelsim parser not yet implemented");
}

// ============================================================================
// ModuleParserFactory Implementation
// ============================================================================

std::unique_ptr<ModuleParser> ModuleParserFactory::create(const std::string& simulator_name) {
  if (simulator_name == "Verilator") {
    return std::unique_ptr<ModuleParser>(new VerilatorModuleParser());
  } else if (simulator_name == "VCS") {
    return std::unique_ptr<ModuleParser>(new VCSModuleParser());
  } else if (simulator_name == "Modelsim") {
    return std::unique_ptr<ModuleParser>(new ModelsimModuleParser());
  } else {
    std::cerr << "Unknown simulator: " << simulator_name << std::endl;
    return nullptr;
  }
}
