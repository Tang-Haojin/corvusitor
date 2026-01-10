#include "code_generator.h"
#include "module_parser.h"
#include "corvus_generator.h"
#include "corvus_cmodel_generator.h"
#include <algorithm>
#include <iostream>

CodeGenerator::CodeGenerator(const std::string& modules_dir,
                             int mbus_count,
                             int sbus_count,
                             GenerationTarget target)
  : modules_dir_(modules_dir),
    conn_builder_(nullptr),
    total_connections_(0),
    vlwide_ports_(0),
    top_outputs_(0),
    mbus_count_(std::max(1, mbus_count)),
    sbus_count_(std::max(1, sbus_count)),
    target_(target),
    target_generator_(nullptr) {
  set_target(target_);
}

CodeGenerator::CodeGenerator(const std::string& modules_dir,
                             SimulatorFactory::SimulatorType /* simulator_type */,
                             int mbus_count,
                             int sbus_count,
                             GenerationTarget target)
  : CodeGenerator(modules_dir, mbus_count, sbus_count, target) {}

bool CodeGenerator::load_data() {
  std::cout << "\n=== Loading Module Data ===" << std::endl;

  ModuleDiscoveryManager manager;
  auto discovery_results = manager.discover_all_modules(modules_dir_);
  manager.print_discovery_statistics();

  modules_list_.clear();
  if (discovery_results.empty()) {
    std::cerr << "No modules found in directory: " << modules_dir_ << std::endl;
    return false;
  }

  for (const auto& result : discovery_results) {
    std::cout << "  Parsing " << result.header_path << " [" << result.simulator_name << "]..." << std::endl;

    std::unique_ptr<ModuleParser> parser = ModuleParserFactory::create(result.simulator_name);
    if (!parser) {
      std::cerr << "Failed to create parser for simulator: " << result.simulator_name << std::endl;
      return false;
    }

    ModuleInfo info = parser->parse(result.header_path);
    if (info.ports.empty()) {
      std::cerr << "Failed to parse module: " << result.module_name << std::endl;
      return false;
    }

    modules_[result.module_name] = info;
    modules_list_.push_back(info);
    std::cout << "    -> " << info.ports.size() << " ports" << std::endl;
  }

  // Build corvus-aware connection analysis
  std::cout << "\n=== Building Connections (Corvus) ===" << std::endl;
  conn_builder_ = std::unique_ptr<ConnectionBuilder>(new ConnectionBuilder());
  analysis_ = conn_builder_->analyze(modules_list_);
  total_connections_ = static_cast<int>(
      analysis_.top_inputs.size() +
      analysis_.top_outputs.size() +
      analysis_.external_inputs.size() +
      analysis_.external_outputs.size());
  for (const auto& kv : analysis_.partitions) {
    total_connections_ += static_cast<int>(kv.second.local_cts_to_si.size());
    total_connections_ += static_cast<int>(kv.second.local_stc_to_ci.size());
    total_connections_ += static_cast<int>(kv.second.remote_s_to_c.size());
  }

  std::cout << "  Total connections: " << total_connections_ << std::endl;
  if (!analysis_.warnings.empty()) {
    std::cout << "  Warnings: " << analysis_.warnings.size() << std::endl;
    for (const auto& w : analysis_.warnings) {
      std::cout << "    - " << w << std::endl;
    }
  }

  return true;
}

bool CodeGenerator::generate_all(const std::string& output_file_base) {
  if (!target_generator_) {
    std::cerr << "No target generator configured." << std::endl;
    return false;
  }
  if (!conn_builder_) {
    std::cerr << "Connection builder not initialized; call load_data first." << std::endl;
    return false;
  }

  std::cout << "\n=== Generating Corvus artifacts ===" << std::endl;
  return target_generator_->generate(analysis_, output_file_base, mbus_count_, sbus_count_);
}

void CodeGenerator::print_statistics() const {
  std::cout << "\n=== Statistics ===" << std::endl;
  std::cout << "  Total modules: " << modules_.size() << std::endl;
  std::cout << "  Total connections: " << total_connections_ << std::endl;
  std::cout << "  Top inputs: " << analysis_.top_inputs.size() << std::endl;
  std::cout << "  Top outputs: " << analysis_.top_outputs.size() << std::endl;
  std::cout << "  External inputs (Ei): " << analysis_.external_inputs.size() << std::endl;
  std::cout << "  External outputs (Eo): " << analysis_.external_outputs.size() << std::endl;
  std::cout << "  Partitions: " << analysis_.partitions.size() << std::endl;
  int local_cts = 0, local_stc = 0, remote = 0;
  for (const auto& kv : analysis_.partitions) {
    local_cts += static_cast<int>(kv.second.local_cts_to_si.size());
    local_stc += static_cast<int>(kv.second.local_stc_to_ci.size());
    remote += static_cast<int>(kv.second.remote_s_to_c.size());
  }
  std::cout << "    localCtSi: " << local_cts << std::endl;
  std::cout << "    localStCi: " << local_stc << std::endl;
  std::cout << "    remoteSitCj: " << remote << std::endl;
  if (!analysis_.warnings.empty()) {
    std::cout << "  Warnings: " << analysis_.warnings.size() << std::endl;
  }
}

void CodeGenerator::set_target(GenerationTarget target) {
  target_ = target;
  switch (target_) {
  case GenerationTarget::Corvus:
    target_generator_ = std::unique_ptr<TargetGenerator>(new CorvusGenerator());
    break;
  case GenerationTarget::CorvusCModel:
    target_generator_ = std::unique_ptr<TargetGenerator>(new CorvusCModelGenerator());
    break;
  }
}
