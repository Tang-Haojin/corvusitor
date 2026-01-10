#include "corvus_cmodel_generator.h"
#include "corvus_generator.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string sanitize_guard(const std::string& base) {
  std::string guard = "CORVUS_CMODEL_GEN_";
  for (char c : base) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      guard.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    } else {
      guard.push_back('_');
    }
  }
  guard.append("_H");
  return guard;
}

std::vector<int> sorted_partition_ids(const ConnectionAnalysis& analysis) {
  std::vector<int> ids;
  ids.reserve(analysis.partitions.size());
  for (const auto& kv : analysis.partitions) {
    ids.push_back(kv.first);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::string path_basename(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

} // namespace

bool CorvusCModelGenerator::generate(const ConnectionAnalysis& analysis,
                                     const std::string& output_base,
                                     int mbus_count,
                                     int sbus_count) {
  // Reuse the base corvus generator for JSON + corvus_gen.h.
  CorvusGenerator corvus_gen;
  if (!corvus_gen.generate(analysis, output_base, mbus_count, sbus_count)) {
    return false;
  }

  auto partition_ids = sorted_partition_ids(analysis);
  if (partition_ids.empty()) {
    std::cerr << "CorvusCModelGenerator requires at least one partition/worker\n";
    return false;
  }
  const int max_pid = partition_ids.back();
  const uint32_t worker_count = static_cast<uint32_t>(partition_ids.size());
  const uint32_t endpoint_count = static_cast<uint32_t>(max_pid + 2); // top (0) + worker ids +1

  std::string header_path = output_base + "_corvus_cmodel_gen.h";
  std::ofstream os(header_path);
  if (!os.is_open()) {
    std::cerr << "Failed to open cmodel header for write: " << header_path << "\n";
    return false;
  }

  std::string guard = sanitize_guard(output_base);
  os << "#ifndef " << guard << "\n";
  os << "#define " << guard << "\n\n";
  os << "#include <cassert>\n";
  os << "#include <cstdint>\n";
  os << "#include <memory>\n";
  os << "#include <utility>\n";
  os << "#include <vector>\n\n";
  os << "#include \"" << path_basename(output_base + "_corvus_gen.h") << "\"\n";
  os << "#include \"boilerplate/corvus_cmodel/corvus_cmodel_idealized_bus.h\"\n";
  os << "#include \"boilerplate/corvus_cmodel/corvus_cmodel_sync_tree.h\"\n";
  os << "#include \"boilerplate/corvus_cmodel/corvus_cmodel_sim_worker_runner.h\"\n\n";
  os << "namespace corvus_generated {\n\n";
  os << "constexpr uint32_t kCorvusCModelWorkerCount = " << worker_count << ";\n";
  os << "constexpr uint32_t kCorvusCModelEndpointCount = " << endpoint_count << ";\n";
  os << "constexpr uint32_t kCorvusCModelMBusCount = kCorvusGenMBusCount;\n";
  os << "constexpr uint32_t kCorvusCModelSBusCount = kCorvusGenSBusCount;\n";
  os << "static_assert(kCorvusCModelWorkerCount > 0, \"CModel requires at least one worker\");\n";
  os << "static constexpr uint32_t kCorvusCModelWorkerIds[kCorvusCModelWorkerCount] = {";
  for (size_t i = 0; i < partition_ids.size(); ++i) {
    os << partition_ids[i];
    if (i + 1 < partition_ids.size()) os << ", ";
  }
  os << "};\n\n";

  os << "class CorvusCModelGen {\n";
  os << "public:\n";
  os << "  CorvusCModelGen();\n";
  os << "  ~CorvusCModelGen();\n\n";
  os << "  CorvusTopModuleGen* top() const { return top_.get(); }\n";
  os << "  CorvusTopModuleGen::TopPortsGen* ports() const {\n";
  os << "    return top_ ? static_cast<CorvusTopModuleGen::TopPortsGen*>(top_->topPorts) : nullptr;\n";
  os << "  }\n";
  os << "  const std::vector<std::shared_ptr<CorvusSimWorker>>& workers() const { return workers_; }\n\n";
  os << "  void startWorkers();\n";
  os << "  void stopWorkers();\n";
  os << "  void reset();\n";
  os << "  void eval();\n";
  os << "  void evalE();\n";
  os << "\nprivate:\n";
  os << "  void buildBuses();\n";
  os << "  void buildTop();\n";
  os << "  void buildWorkers();\n";
  os << "  void initModules();\n";
  os << "  void cleanupModules();\n";
  os << "  void ensureInitialized();\n\n";
  os << "  CorvusCModelSyncTree syncTree_;\n";
  os << "  std::shared_ptr<CorvusCModelMasterSynctreeEndpoint> masterEndpoint_;\n";
  os << "  std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>> simEndpoints_;\n";
  os << "  std::vector<std::shared_ptr<CorvusCModelIdealizedBus>> mBuses_;\n";
  os << "  std::vector<std::shared_ptr<CorvusCModelIdealizedBus>> sBuses_;\n";
  os << "  std::vector<CorvusBusEndpoint*> topMBusEndpoints_;\n";
  os << "  std::shared_ptr<CorvusTopModuleGen> top_;\n";
  os << "  std::vector<std::shared_ptr<CorvusSimWorker>> workers_;\n";
  os << "  std::unique_ptr<CorvusCModelSimWorkerRunner> runner_;\n";
  os << "  bool initialized_ = false;\n";
  os << "};\n\n";

  os << "inline CorvusCModelGen::CorvusCModelGen()\n";
  os << "    : syncTree_(kCorvusCModelWorkerCount),\n";
  os << "      masterEndpoint_(syncTree_.getMasterEndpoint()),\n";
  os << "      simEndpoints_(syncTree_.getSimCoreEndpoints()) {\n";
  os << "  buildBuses();\n";
  os << "  buildTop();\n";
  os << "  buildWorkers();\n";
  os << "}\n\n";

  os << "inline CorvusCModelGen::~CorvusCModelGen() {\n";
  os << "  stopWorkers();\n";
  os << "  cleanupModules();\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::buildBuses() {\n";
  os << "  topMBusEndpoints_.reserve(kCorvusCModelMBusCount);\n";
  os << "  mBuses_.reserve(kCorvusCModelMBusCount);\n";
  os << "  for (uint32_t i = 0; i < kCorvusCModelMBusCount; ++i) {\n";
  os << "    auto bus = std::make_shared<CorvusCModelIdealizedBus>(kCorvusCModelEndpointCount);\n";
  os << "    topMBusEndpoints_.push_back(bus->getEndpoint(0).get());\n";
  os << "    mBuses_.push_back(std::move(bus));\n";
  os << "  }\n";
  os << "  sBuses_.reserve(kCorvusCModelSBusCount);\n";
  os << "  for (uint32_t i = 0; i < kCorvusCModelSBusCount; ++i) {\n";
  os << "    sBuses_.push_back(std::make_shared<CorvusCModelIdealizedBus>(kCorvusCModelEndpointCount));\n";
  os << "  }\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::buildTop() {\n";
  os << "  top_ = std::make_shared<CorvusTopModuleGen>(masterEndpoint_.get(), topMBusEndpoints_);\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::buildWorkers() {\n";
  os << "  workers_.reserve(kCorvusCModelWorkerCount);\n";
  for (size_t idx = 0; idx < partition_ids.size(); ++idx) {
    int pid = partition_ids[idx];
    os << "  {\n";
    os << "    std::vector<CorvusBusEndpoint*> mEndpoints;\n";
    os << "    mEndpoints.reserve(kCorvusCModelMBusCount);\n";
    os << "    for (uint32_t b = 0; b < kCorvusCModelMBusCount; ++b) {\n";
    os << "      mEndpoints.push_back(mBuses_[b]->getEndpoint(" << (pid + 1) << ").get());\n";
    os << "    }\n";
    os << "    std::vector<CorvusBusEndpoint*> sEndpoints;\n";
    os << "    sEndpoints.reserve(kCorvusCModelSBusCount);\n";
    os << "    for (uint32_t b = 0; b < kCorvusCModelSBusCount; ++b) {\n";
    os << "      sEndpoints.push_back(sBuses_[b]->getEndpoint(" << (pid + 1) << ").get());\n";
    os << "    }\n";
    os << "    auto worker = std::make_shared<CorvusSimWorkerGenP" << pid << ">(simEndpoints_.at(" << idx << ").get(), mEndpoints, sEndpoints);\n";
    os << "    workers_.push_back(worker);\n";
    os << "  }\n";
  }
  os << "  runner_ = std::unique_ptr<CorvusCModelSimWorkerRunner>(new CorvusCModelSimWorkerRunner(workers_));\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::initModules() {\n";
  os << "  if (initialized_) return;\n";
  os << "  if (top_) top_->init();\n";
  os << "  for (auto& w : workers_) {\n";
  os << "    if (w) w->init();\n";
  os << "  }\n";
  os << "  initialized_ = true;\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::cleanupModules() {\n";
  os << "  if (!initialized_) return;\n";
  os << "  for (auto& w : workers_) {\n";
  os << "    if (w) w->cleanup();\n";
  os << "  }\n";
  os << "  if (top_) top_->cleanup();\n";
  os << "  initialized_ = false;\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::ensureInitialized() {\n";
  os << "  if (!initialized_) {\n";
  os << "    initModules();\n";
  os << "  }\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::startWorkers() {\n";
  os << "  ensureInitialized();\n";
  os << "  if (runner_) runner_->run();\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::stopWorkers() {\n";
  os << "  if (runner_) runner_->stop();\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::reset() {\n";
  os << "  ensureInitialized();\n";
  os << "  if (top_) top_->resetSimWorker();\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::eval() {\n";
  os << "  ensureInitialized();\n";
  os << "  if (top_) top_->eval();\n";
  os << "}\n\n";

  os << "inline void CorvusCModelGen::evalE() {\n";
  os << "  ensureInitialized();\n";
  os << "  if (top_) top_->evalE();\n";
  os << "}\n\n";

  os << "} // namespace corvus_generated\n";
  os << "#endif // " << guard << "\n";
  return true;
}
