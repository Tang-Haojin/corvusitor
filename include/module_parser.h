#ifndef MODULE_PARSER_H
#define MODULE_PARSER_H

#include "module_info.h"
#include <string>
#include <regex>

class ModuleParser {
public:
  ModuleParser();

  // Main parse function
  ModuleInfo parse(const std::string& header_path);

private:
  // Parse a single port macro
  // Input: "VL_IN8(&signal_name, 7, 0);"
  // Output: PortInfo
  PortInfo parse_port_macro(const std::string& line);

  // Infer type from module name
  // "corvus_comb_P0" -> COMB
  // "corvus_seq_P1"  -> SEQ
  // "corvus_external" -> EXTERNAL
  ModuleType infer_module_type(const std::string& module_name);

  // Extract partition ID
  // "corvus_comb_P0" -> 0
  // "corvus_seq_P1"  -> 1
  // "corvus_external" -> -1
  int extract_partition_id(const std::string& module_name);

  // Generate instance name
  // "Vcorvus_comb_P0" -> "comb_p0"
  std::string generate_instance_name(const std::string& class_name, ModuleType type);

  // Infer static library path
  std::string infer_lib_path(const std::string& header_path, const std::string& class_name);

  // Regular expressions
  std::regex port_macro_regex;       // 3-argument format: VL_(IN|OUT)(8|16|64|)
  std::regex port_macro_regex_wide;  // 4-argument format: VL_(IN|OUT)W
};

#endif // MODULE_PARSER_H
