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
#include "connection_analysis.h"

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
  enum class GenerationTarget {
    Corvus,
    CorvusCModel
  };

  /**
   * Target generator abstraction
   */
  class TargetGenerator {
  public:
    virtual ~TargetGenerator() = default;
    virtual bool generate(const ConnectionAnalysis& analysis,
                          const std::string& output_base,
                          int mbus_count,
                          int sbus_count) = 0;
  };

  /**
   * Constructor with automatic simulator detection
   * @param modules_dir Directory containing simulator output
   */
  CodeGenerator(const std::string& modules_dir,
                int mbus_count = 1,
                int sbus_count = 1,
                GenerationTarget target = GenerationTarget::Corvus);

  /**
   * Constructor with explicit simulator type
   * @param modules_dir Directory containing simulator output
   * @param simulator_type Type of simulator to use
   */
  CodeGenerator(const std::string& modules_dir, 
                SimulatorFactory::SimulatorType simulator_type,
                int mbus_count = 1,
                int sbus_count = 1,
                GenerationTarget target = GenerationTarget::Corvus);

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
   * Print statistics
   */
  void print_statistics() const;

  /**
   * Switch the generation target (corvus or cmodel) and reset the target generator.
   */
  void set_target(GenerationTarget target);

private:
  std::string modules_dir_;  // Module directory
  std::map<std::string, ModuleInfo> modules_;  // Module information
  std::vector<ModuleInfo> modules_list_; // Persistent storage for analysis pointers
  std::unique_ptr<ConnectionBuilder> conn_builder_;  // Connection builder
  ConnectionAnalysis analysis_;      // Corvus-classified connection graph

  // Connection statistics
  int total_connections_;
  int vlwide_ports_;
  int top_outputs_;

  int mbus_count_;
  int sbus_count_;
  GenerationTarget target_;
  std::unique_ptr<TargetGenerator> target_generator_;
};

#endif // CODE_GENERATOR_H
