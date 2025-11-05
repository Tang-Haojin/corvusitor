#ifndef CODE_GENERATOR_H
#define CODE_GENERATOR_H

#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <memory>
#include "module_info.h"
#include "connection_builder.h"
#include "simulator_interface.h"

/**
 * CodeGenerator - Generate VCorvusTopWrapper connection propagation code
 *
 * Features:
 * 1. Read connection graph data
 * 2. Generate propagate_*() function implementations
 * 3. Generate top-level output getter methods
 * 4. Special handling for VlWide<N> type
 */
class CodeGenerator {
public:
  /**
   * Constructor with automatic simulator detection
   * @param modules_dir Directory containing simulator output
   */
  CodeGenerator(const std::string& modules_dir);

  /**
   * Constructor with explicit simulator type
   * @param modules_dir Directory containing simulator output
   * @param simulator_type Type of simulator to use
   */
  CodeGenerator(const std::string& modules_dir, 
                SimulatorFactory::SimulatorType simulator_type);

  /**
   * Load module and connection data
   */
  bool load_data();

  /**
   * Generate all connection code
   * @param output_cpp_file Output implementation file path
   * @param output_h_file Output header file path (with port declarations)
   * @return Returns true on success
   */
  bool generate_all(const std::string& output_file_base);

  /**
   * Generate Makefile
   * @param output_file_base Makefile base name without extension
   * @return Returns true on success
   */
  bool generate_makefile(const std::string& output_file_base);

  /**
   * Generate a single propagate function
   * @param connections Connection list
   * @param function_name Function name
   * @param comment Function comment
   */
  std::string generate_propagate_function(
    const std::vector<PortConnection>& connections,
    const std::string& function_name,
    const std::string& comment
  );

  /**
   * Generate top-level input/output port declarations (for header file)
   * @param top_inputs Top-level input connection list
   * @param top_outputs Top-level output connection list
   * @return Port declaration code (C++ public member variables)
   */
  std::string generate_port_declarations(
    const std::vector<PortConnection>& top_inputs,
    const std::vector<PortConnection>& top_outputs
  );

  /**
   * Print statistics
   */
  void print_statistics() const;

private:
  std::string modules_dir_;  // Module directory
  std::map<std::string, ModuleInfo> modules_;  // Module information
  ConnectionBuilder* conn_builder_;  // Connection builder

  // Connection statistics
  int total_connections_;
  int vlwide_ports_;
  int top_outputs_;

  /**
   * Generate a single assignment statement
   * @param conn Connection information
   * @return C++ assignment statement
   */
  std::string generate_assignment(const PortConnection& conn);

  /**
   * Check if type is VlWide
   */
  bool is_vlwide_type(const std::string& type_str) const;

  /**
   * Extract VlWide width parameter
   */
  int extract_vlwide_width(const std::string& type_str) const;
};

#endif // CODE_GENERATOR_H
