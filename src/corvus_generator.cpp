#include "corvus_generator.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct ChunkPlan {
  int data_bits = 0;
  int chunk_bits = 0;
  int chunk_count = 1;
};

int slot_bits_for_count(size_t count) {
  int bits = 0;
  size_t value = (count == 0) ? 0 : (count - 1);
  while (value > 0) {
    bits++;
    value >>= 1;
  }
  if (bits <= 8) return 8;
  if (bits <= 16) return 16;
  return 32;
}

ChunkPlan compute_chunk_plan(int width, int slot_bits) {
  ChunkPlan plan;
  auto choose_data_bits = [&](int chunk_bits) -> int {
    int space = 48 - slot_bits - chunk_bits;
    int choice = 0;
    for (int candidate : {32, 16, 8}) {
      if (candidate <= space && candidate > choice) {
        choice = candidate;
      }
    }
    return choice;
  };

  int data_candidate = choose_data_bits(0);
  if (width <= data_candidate) {
    plan.data_bits = data_candidate;
    plan.chunk_bits = 0;
    plan.chunk_count = 1;
    return plan;
  }

  for (int chunk_bits : {8, 16, 32}) {
    int data_bits = choose_data_bits(chunk_bits);
    if (data_bits <= 0) continue;
    int chunk_count = (width + data_bits - 1) / data_bits;
    if (chunk_count <= (1 << chunk_bits)) {
      plan.data_bits = data_bits;
      plan.chunk_bits = chunk_bits;
      plan.chunk_count = chunk_count;
      return plan;
    }
  }

  // Fallback (should not normally hit)
  plan.chunk_bits = 8;
  plan.data_bits = choose_data_bits(plan.chunk_bits);
  if (plan.data_bits <= 0) {
    plan.data_bits = 8;
  }
  plan.chunk_count = (width + plan.data_bits - 1) / plan.data_bits;
  if (plan.chunk_count <= 0) plan.chunk_count = 1;
  return plan;
}

int array_size_from_endpoint(const SignalEndpoint& ep, PortWidthType width_type) {
  if (width_type != PortWidthType::VL_W) {
    return 0;
  }
  if (ep.port) {
    return ep.port->array_size;
  }
  return 0;
}

std::string cpp_type_from_endpoint(const SignalEndpoint& ep,
                                   PortWidthType width_type,
                                   int array_size) {
  if (ep.port) {
    return ep.port->get_cpp_type();
  }
  switch (width_type) {
  case PortWidthType::VL_8: return "CData";
  case PortWidthType::VL_16: return "SData";
  case PortWidthType::VL_32: return "IData";
  case PortWidthType::VL_64: return "QData";
  case PortWidthType::VL_W:
    return "VlWide<" + std::to_string(array_size > 0 ? array_size : 1) + ">";
  }
  return "IData";
}

struct SignalRef {
  std::string name;
  int width = 0;
  PortWidthType width_type = PortWidthType::VL_8;
  int array_size = 0;
  SignalEndpoint driver;
  SignalEndpoint receiver;
  int driver_pid = -1;
  int receiver_pid = -1;
};

SignalRef build_signal(const ClassifiedConnection& conn,
                       const SignalEndpoint& receiver) {
  SignalRef ref;
  ref.name = conn.port_name;
  ref.width = conn.width;
  ref.width_type = conn.width_type;
  ref.driver = conn.driver;
  ref.receiver = receiver;
  ref.driver_pid = conn.driver.module ? conn.driver.module->partition_id : -1;
  ref.receiver_pid = receiver.module ? receiver.module->partition_id : -1;
  // Prefer driver for VL_W array size, otherwise fallback to receiver.
  if (conn.driver.port && conn.driver.port->width_type == PortWidthType::VL_W) {
    ref.array_size = conn.driver.port->array_size;
  } else {
    ref.array_size = array_size_from_endpoint(receiver, conn.width_type);
  }
  return ref;
}

struct DownlinkSlot {
  SignalRef sig;
  bool from_external = false;
  int slot_id = 0;
  int slot_bits = 8;
  int bus_index = 0;
  ChunkPlan chunk;
};

struct TopSlot {
  SignalRef sig;
  bool to_external = false;
  int slot_id = 0;
  int slot_bits = 8;
  int bus_index = 0;
  ChunkPlan chunk;
};

struct RemoteRecvSlot {
  SignalRef sig;
  int from_pid = -1;
  int slot_id = 0;
  int slot_bits = 8;
  int bus_index = 0;
  ChunkPlan chunk;
};

struct WorkerPlan {
  int pid = -1;
  const ModuleInfo* comb = nullptr;
  const ModuleInfo* seq = nullptr;
  std::vector<DownlinkSlot> downlinks;
  std::vector<RemoteRecvSlot> remote_recv;
  std::vector<ClassifiedConnection> local_cts;
  std::vector<ClassifiedConnection> local_stc;
  int mbus_slot_bits = 8;
  int remote_slot_bits = 8;
};

void write_endpoint(std::ostream& os, const SignalEndpoint& ep) {
  os << "{ \"module\": \"" << (ep.module ? ep.module->instance_name : "null")
     << "\", \"port\": \"" << (ep.port ? ep.port->name : "null") << "\" }";
}

void write_connections(std::ostream& os, const std::vector<ClassifiedConnection>& conns) {
  os << "[";
  for (size_t i = 0; i < conns.size(); ++i) {
    const auto& c = conns[i];
    os << "{ \"port\": \"" << c.port_name << "\", \"width\": " << c.width
       << ", \"width_type\": " << static_cast<int>(c.width_type)
       << ", \"driver\": ";
    write_endpoint(os, c.driver);
    os << ", \"receivers\": [";
    for (size_t j = 0; j < c.receivers.size(); ++j) {
      write_endpoint(os, c.receivers[j]);
      if (j + 1 < c.receivers.size()) os << ", ";
    }
    os << "] }";
    if (i + 1 < conns.size()) os << ", ";
  }
  os << "]";
}

void register_module(const SignalEndpoint& ep, WorkerPlan& plan,
                     const ModuleInfo*& external_mod) {
  if (!ep.module) return;
  switch (ep.module->type) {
  case ModuleType::COMB:
    if (!plan.comb) plan.comb = ep.module;
    break;
  case ModuleType::SEQ:
    if (!plan.seq) plan.seq = ep.module;
    break;
  case ModuleType::EXTERNAL:
    if (!external_mod) external_mod = ep.module;
    break;
  }
}

std::string sanitize_guard(const std::string& base) {
  std::string guard = "CORVUS_GEN_";
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

std::string path_basename(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

std::string cpp_type_from_signal(const SignalRef& sig) {
  if (sig.driver.port) {
    return sig.driver.port->get_cpp_type();
  }
  if (sig.receiver.port) {
    return sig.receiver.port->get_cpp_type();
  }
  return cpp_type_from_endpoint(sig.driver, sig.width_type, sig.array_size);
}

void write_includes(std::ostream& os, const std::set<std::string>& module_headers) {
  os << "#include <algorithm>\n";
  os << "#include <cassert>\n";
  os << "#include <cstddef>\n";
  os << "#include <cstdint>\n";
  os << "#include <cstring>\n";
  os << "#include <memory>\n";
  os << "#include <unordered_map>\n";
  os << "#include <utility>\n";
  os << "#include <vector>\n\n";
  os << "#include \"boilerplate/common/module_handle.h\"\n";
  os << "#include \"boilerplate/common/top_ports.h\"\n";
  os << "#include \"boilerplate/corvus/corvus_top_module.h\"\n";
  os << "#include \"boilerplate/corvus/corvus_sim_worker.h\"\n";
  os << "#include \"boilerplate/corvus/corvus_codegen_utils.h\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\n";
}

std::string generate_top_header(const std::string& output_base,
                                const ModuleInfo* external_mod,
                                const std::map<std::string, SignalRef>& top_inputs,
                                const std::map<std::string, SignalRef>& top_outputs,
                                int mbus_count,
                                int sbus_count) {
  std::set<std::string> module_headers;
  if (external_mod) module_headers.insert(external_mod->header_path);

  std::ostringstream os;
  std::string guard = sanitize_guard(output_base + "_TOP");
  os << "#ifndef " << guard << "\n";
  os << "#define " << guard << "\n\n";
  write_includes(os, module_headers);
  os << "namespace corvus_generated {\n\n";
  os << "namespace detail = corvus_codegen_detail;\n\n";
  os << "constexpr size_t kCorvusGenMBusCount = " << mbus_count << ";\n";
  os << "constexpr size_t kCorvusGenSBusCount = " << sbus_count << ";\n\n";

  std::string ext_class = external_mod ? external_mod->class_name : "";
  os << "class CorvusTopModuleGen : public CorvusTopModule {\n";
  os << "public:\n";
  os << "  class TopPortsGen : public TopPorts {\n";
  os << "  public:\n";
  for (const auto& kv : top_inputs) {
    os << "    " << cpp_type_from_signal(kv.second) << " " << kv.first << ";\n";
  }
  for (const auto& kv : top_outputs) {
    os << "    " << cpp_type_from_signal(kv.second) << " " << kv.first << ";\n";
  }
  os << "  };\n\n";
  os << "  CorvusTopModuleGen(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,\n";
  os << "                     std::vector<CorvusBusEndpoint*> mBusEndpoints);\n";
  os << "protected:\n";
  os << "  TopPorts* createTopPorts() override;\n";
  os << "  void deleteTopPorts() override;\n";
  os << "  ModuleHandle* createExternalModule() override;\n";
  os << "  void deleteExternalModule() override;\n";
  os << "  void sendIAndEOutput() override;\n";
  os << "  void loadOAndEInput() override;\n";
  os << "};\n\n";
  os << "} // namespace corvus_generated\n";
  os << "#endif // " << guard << "\n";
  return os.str();
}

std::string generate_top_cpp(const std::string& output_base,
                             const ModuleInfo* external_mod,
                             const std::map<int, WorkerPlan>& workers,
                             const std::vector<TopSlot>& top_slots,
                             int top_slot_bits,
                             int mbus_count,
                             int sbus_count) {
  std::set<std::string> module_headers;
  if (external_mod) module_headers.insert(external_mod->header_path);
  (void)mbus_count;
  (void)sbus_count;

  std::ostringstream os;
  os << "#include \"" << path_basename(output_base + "_corvus_top.h") << "\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\nnamespace corvus_generated {\n\n";
  std::string ext_class = external_mod ? external_mod->class_name : "";
  os << "namespace detail = corvus_codegen_detail;\n\n";

  os << "CorvusTopModuleGen::CorvusTopModuleGen(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,\n";
  os << "                                     std::vector<CorvusBusEndpoint*> mBusEndpoints)\n";
  os << "    : CorvusTopModule(masterSynctreeEndpoint, std::move(mBusEndpoints)) {\n";
  os << "  assert(this->mBusEndpoints.size() == kCorvusGenMBusCount && \"MBus endpoint count mismatch\");\n";
  os << "}\n\n";

  os << "TopPorts* CorvusTopModuleGen::createTopPorts() { return new TopPortsGen(); }\n";
  os << "void CorvusTopModuleGen::deleteTopPorts() {\n";
  os << "  delete static_cast<TopPortsGen*>(topPorts);\n";
  os << "  topPorts = nullptr;\n";
  os << "}\n";
  if (!ext_class.empty()) {
    os << "ModuleHandle* CorvusTopModuleGen::createExternalModule() {\n";
    os << "  auto* inst = new " << ext_class << "();\n";
    os << "  return new VerilatorModuleHandle<" << ext_class << ">(inst);\n";
    os << "}\n";
    os << "void CorvusTopModuleGen::deleteExternalModule() {\n";
    os << "  auto* handle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eHandle);\n";
    os << "  if (handle) {\n";
    os << "    delete handle->mp;\n";
    os << "    delete handle;\n";
    os << "    eHandle = nullptr;\n";
    os << "  }\n";
    os << "}\n";
  } else {
    os << "ModuleHandle* CorvusTopModuleGen::createExternalModule() { return nullptr; }\n";
    os << "void CorvusTopModuleGen::deleteExternalModule() { eHandle = nullptr; }\n";
  }

  // sendIAndEOutput
  os << "void CorvusTopModuleGen::sendIAndEOutput() {\n";
  os << "  auto* ports = static_cast<TopPortsGen*>(topPorts);\n";
  if (!ext_class.empty()) {
    os << "  auto* extHandle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eHandle);\n";
    os << "  auto* ext = extHandle ? extHandle->mp : nullptr;\n";
  } else {
    os << "  void* ext = nullptr;\n";
  }
  os << "  if (!ports) return;\n";
  for (const auto& kv : workers) {
    const auto& wp = kv.second;
    if (wp.downlinks.empty()) continue;
    os << "  {\n";
    os << "    const uint32_t targetId = " << (kv.first + 1) << ";\n";
    os << "    const uint8_t slotBits = " << wp.mbus_slot_bits << ";\n";
    for (const auto& slot : wp.downlinks) {
      os << "    {\n";
      os << "      CorvusBusEndpoint* ep = mBusEndpoints[" << slot.bus_index << "];\n";
      os << "      const uint8_t dataBits = " << slot.chunk.data_bits << ";\n";
      os << "      const uint8_t chunkBits = " << slot.chunk.chunk_bits << ";\n";
      os << "      const uint32_t chunkCount = " << slot.chunk.chunk_count << ";\n";
      os << "      const uint32_t slotId = " << slot.slot_id << ";\n";
      if (slot.sig.width_type == PortWidthType::VL_W) {
        std::string src = slot.from_external ? (ext_class.empty() ? "nullptr" : ("ext ? reinterpret_cast<const uint32_t*>(&ext->" + slot.sig.name + ") : nullptr"))
                                             : ("reinterpret_cast<const uint32_t*>(&ports->" + slot.sig.name + ")");
        os << "      const uint32_t* widePtr = " << src << ";\n";
        os << "      if (widePtr) {\n";
        os << "        for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {\n";
        os << "          uint64_t chunkData = detail::read_wide_chunk(widePtr, " << slot.sig.array_size << ", dataBits, chunkIdx);\n";
        os << "          uint64_t payload = detail::pack_payload(slotId, chunkData, chunkIdx, slotBits, dataBits, chunkBits);\n";
        os << "          ep->send(targetId, payload);\n";
        os << "        }\n";
        os << "      }\n";
      } else {
        std::string base = slot.from_external ? (ext_class.empty() ? "0ULL" : ("(ext ? static_cast<uint64_t>(ext->" + slot.sig.name + ") : 0ULL)"))
                                             : ("static_cast<uint64_t>(ports->" + slot.sig.name + ")");
        os << "      uint64_t value = " << base << ";\n";
        os << "      for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {\n";
        os << "        uint64_t chunkData = (value >> (chunkIdx * dataBits)) & detail::mask_bits(dataBits);\n";
        os << "        uint64_t payload = detail::pack_payload(slotId, chunkData, chunkIdx, slotBits, dataBits, chunkBits);\n";
        os << "        ep->send(targetId, payload);\n";
        os << "      }\n";
      }
      os << "    }\n";
    }
    os << "  }\n";
  }
  os << "}\n\n";

  // loadOAndEInput
  os << "void CorvusTopModuleGen::loadOAndEInput() {\n";
  os << "  auto* ports = static_cast<TopPortsGen*>(topPorts);\n";
  if (!ext_class.empty()) {
    os << "  auto* extHandle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eHandle);\n";
    os << "  auto* ext = extHandle ? extHandle->mp : nullptr;\n";
  } else {
    os << "  void* ext = nullptr;\n";
  }
  os << "  const uint8_t slotBits = " << top_slot_bits << ";\n";
  std::vector<const TopSlot*> top_direct;
  std::vector<const TopSlot*> top_decode;
  for (const auto& slot : top_slots) {
    if (slot.chunk.chunk_count == 1 && slot.sig.width_type != PortWidthType::VL_W) {
      top_direct.push_back(&slot);
    } else {
      top_decode.push_back(&slot);
    }
  }
  if (!top_decode.empty()) {
    os << "  std::vector<detail::SlotDecoder> decoders;\n";
    os << "  std::unordered_map<uint32_t, size_t> slotIndex;\n";
    os << "  decoders.reserve(" << top_decode.size() << ");\n";
    for (const auto* slot : top_decode) {
      os << "  decoders.emplace_back(" << slot->slot_id << ", " << slot->slot_bits << ", "
         << slot->chunk.data_bits << ", " << slot->chunk.chunk_bits << ", " << slot->chunk.chunk_count << ");\n";
      os << "  slotIndex[" << slot->slot_id << "] = decoders.size() - 1;\n";
    }
  }
  os << "  for (auto* ep : mBusEndpoints) {\n";
  os << "    if (!ep) continue;\n";
  os << "    while (ep->bufferCnt() > 0) {\n";
  os << "      uint64_t payload = ep->recv();\n";
  os << "      uint32_t slotId = static_cast<uint32_t>(payload & detail::mask_bits(slotBits));\n";
  if (!top_direct.empty()) {
    os << "      bool handled = false;\n";
    os << "      switch (slotId) {\n";
    for (const auto* slot : top_direct) {
      std::string dest = slot->to_external
        ? (ext_class.empty() ? "" : ("ext->" + slot->sig.name))
        : ("ports->" + slot->sig.name);
      os << "      case " << slot->slot_id << ": {\n";
      os << "        uint64_t data = (payload >> slotBits) & detail::mask_bits(" << slot->sig.width << ");\n";
      if (slot->to_external && ext_class.empty()) {
        os << "        // External module not available in this build\n";
      } else {
        os << "        " << dest << " = static_cast<" << cpp_type_from_signal(slot->sig)
           << ">(data);\n";
      }
      os << "        handled = true;\n";
      os << "        break;\n";
      os << "      }\n";
    }
    os << "      default: break;\n";
    os << "      }\n";
  }
  if (!top_decode.empty()) {
    if (!top_direct.empty()) {
      os << "      if (handled) continue;\n";
    }
    os << "      auto it = slotIndex.find(slotId);\n";
    os << "      if (it == slotIndex.end()) continue;\n";
    os << "      decoders[it->second].consume(payload);\n";
  }
  os << "    }\n";
  os << "  }\n";
  if (!top_decode.empty()) {
    os << "  for (size_t idx = 0; idx < decoders.size(); ++idx) {\n";
    os << "    if (!decoders[idx].complete()) continue;\n";
    os << "    switch (idx) {\n";
    size_t slot_idx = 0;
    for (const auto* slot : top_decode) {
      std::string dest = slot->to_external
        ? (ext_class.empty() ? "" : ("ext->" + slot->sig.name))
        : ("ports->" + slot->sig.name);
      os << "    case " << slot_idx << ": {\n";
      if (slot->to_external && ext_class.empty()) {
        os << "      // External module is not available; skip " << slot->sig.name << "\n";
      } else if (slot->sig.width_type == PortWidthType::VL_W) {
        os << "      detail::apply_to_wide(decoders[idx], reinterpret_cast<uint32_t*>(&" << dest << "), " << slot->sig.array_size << ");\n";
      } else {
        os << "      uint64_t value = detail::assemble_scalar(decoders[idx]);\n";
        os << "      " << dest << " = static_cast<" << cpp_type_from_signal(slot->sig) << ">(value & detail::mask_bits(" << slot->sig.width << "));\n";
      }
      os << "      break;\n";
      os << "    }\n";
      ++slot_idx;
    }
    os << "    default: break;\n";
    os << "    }\n";
    os << "  }\n";
  }
  os << "}\n\n";

  os << "} // namespace corvus_generated\n";
  return os.str();
}

std::string generate_worker_header(const std::string& output_base,
                                   const WorkerPlan& wp,
                                   int mbus_count,
                                   int sbus_count,
                                   const std::set<std::string>& module_headers) {
  std::ostringstream os;
  std::string guard = sanitize_guard(output_base + "_WORKER_P" + std::to_string(wp.pid));
  os << "#ifndef " << guard << "\n";
  os << "#define " << guard << "\n\n";
  write_includes(os, module_headers);
  os << "namespace corvus_generated {\n\n";
  os << "namespace detail = corvus_codegen_detail;\n\n";
  os << "constexpr size_t kCorvusGenMBusCount = " << mbus_count << ";\n";
  os << "constexpr size_t kCorvusGenSBusCount = " << sbus_count << ";\n\n";

  os << "class CorvusSimWorkerGenP" << wp.pid << " : public CorvusSimWorker {\n";
  os << "public:\n";
  os << "  CorvusSimWorkerGenP" << wp.pid << "(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,\n";
  os << "                                     std::vector<CorvusBusEndpoint*> mBusEndpoints,\n";
  os << "                                     std::vector<CorvusBusEndpoint*> sBusEndpoints);\n";
  os << "protected:\n";
  os << "  void createSimModules() override;\n";
  os << "  void deleteSimModules() override;\n";
  os << "  void loadRemoteCInputs() override;\n";
  os << "  void sendRemoteCOutputs() override;\n";
  os << "  void loadSInputs() override;\n";
  os << "  void sendRemoteSOutputs() override;\n";
  os << "  void loadLocalCInputs() override;\n";
  os << "};\n\n";
  os << "} // namespace corvus_generated\n";
  os << "#endif // " << guard << "\n";
  return os.str();
}

std::string generate_worker_cpp(const std::string& output_base,
                                const WorkerPlan& wp,
                                int mbus_count,
                                int sbus_count,
                                int top_slot_bits,
                                const std::map<int, std::vector<const TopSlot*>>& send_to_top,
                                const std::map<int, std::vector<const RemoteRecvSlot*>>& remote_send_map,
                                const std::set<std::string>& module_headers) {
  std::ostringstream os;
  os << "#include \"" << path_basename(output_base + "_corvus_worker_p" + std::to_string(wp.pid) + ".h") << "\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\nnamespace corvus_generated {\n\n";
  os << "namespace detail = corvus_codegen_detail;\n\n";
  (void)mbus_count;
  (void)sbus_count;

  os << "CorvusSimWorkerGenP" << wp.pid << "::CorvusSimWorkerGenP" << wp.pid << "(\n";
  os << "    CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,\n";
  os << "    std::vector<CorvusBusEndpoint*> mBusEndpoints,\n";
  os << "    std::vector<CorvusBusEndpoint*> sBusEndpoints)\n";
  os << "    : CorvusSimWorker(simCoreSynctreeEndpoint, std::move(mBusEndpoints), std::move(sBusEndpoints)) {\n";
  os << "  assert(this->mBusEndpoints.size() == kCorvusGenMBusCount && \"MBus endpoint count mismatch\");\n";
  os << "  assert(this->sBusEndpoints.size() == kCorvusGenSBusCount && \"SBus endpoint count mismatch\");\n";
  os << "}\n\n";

  os << "void CorvusSimWorkerGenP" << wp.pid << "::createSimModules() {\n";
  os << "  cModule = new VerilatorModuleHandle<" << wp.comb->class_name << ">(new " << wp.comb->class_name << "());\n";
  os << "  sModule = new VerilatorModuleHandle<" << wp.seq->class_name << ">(new " << wp.seq->class_name << "());\n";
  os << "}\n";
  os << "void CorvusSimWorkerGenP" << wp.pid << "::deleteSimModules() {\n";
  os << "  auto* cHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  if (cHandle) { delete cHandle->mp; delete cHandle; }\n";
  os << "  auto* sHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  if (sHandle) { delete sHandle->mp; delete sHandle; }\n";
  os << "  cModule = nullptr; sModule = nullptr;\n";
  os << "}\n\n";

  // loadRemoteCInputs
  os << "void CorvusSimWorkerGenP" << wp.pid << "::loadRemoteCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  if (!wp.downlinks.empty()) {
    os << "  {\n";
    os << "    const uint8_t slotBits = " << wp.mbus_slot_bits << ";\n";
    std::vector<const DownlinkSlot*> direct_slots;
    std::vector<const DownlinkSlot*> decode_slots;
    for (const auto& slot : wp.downlinks) {
      if (slot.chunk.chunk_count == 1 && slot.sig.width_type != PortWidthType::VL_W) {
        direct_slots.push_back(&slot);
      } else {
        decode_slots.push_back(&slot);
      }
    }
    if (!decode_slots.empty()) {
      os << "    std::vector<detail::SlotDecoder> decoders;\n";
      os << "    std::unordered_map<uint32_t, size_t> slotIndex;\n";
      os << "    decoders.reserve(" << decode_slots.size() << ");\n";
      for (const auto* slot : decode_slots) {
        os << "    decoders.emplace_back(" << slot->slot_id << ", " << slot->slot_bits << ", "
           << slot->chunk.data_bits << ", " << slot->chunk.chunk_bits << ", " << slot->chunk.chunk_count << ");\n";
        os << "    slotIndex[" << slot->slot_id << "] = decoders.size() - 1;\n";
      }
    }
    os << "    for (auto* ep : mBusEndpoints) {\n";
    os << "      if (!ep) continue;\n";
    os << "      while (ep->bufferCnt() > 0) {\n";
    os << "        uint64_t payload = ep->recv();\n";
    os << "        uint32_t slotId = static_cast<uint32_t>(payload & detail::mask_bits(slotBits));\n";
    if (!direct_slots.empty()) {
      os << "        bool handled = false;\n";
      os << "        switch (slotId) {\n";
      for (const auto* slot : direct_slots) {
        os << "        case " << slot->slot_id << ": {\n";
        os << "          uint64_t data = (payload >> slotBits) & detail::mask_bits(" << slot->sig.width << ");\n";
        os << "          comb->" << slot->sig.name << " = static_cast<" << cpp_type_from_signal(slot->sig) << ">(data);\n";
        os << "          handled = true;\n";
        os << "          break;\n";
        os << "        }\n";
      }
      os << "        default: break;\n";
      os << "        }\n";
    }
    if (!decode_slots.empty()) {
      if (!direct_slots.empty()) {
        os << "        if (handled) continue;\n";
      }
      os << "        auto it = slotIndex.find(slotId);\n";
      os << "        if (it == slotIndex.end()) continue;\n";
      os << "        decoders[it->second].consume(payload);\n";
    }
    os << "      }\n";
    os << "    }\n";
    if (!decode_slots.empty()) {
      os << "    for (size_t idx = 0; idx < decoders.size(); ++idx) {\n";
      os << "      if (!decoders[idx].complete()) continue;\n";
      os << "      switch (idx) {\n";
      size_t ri = 0;
      for (const auto& slot : wp.downlinks) {
        if (slot.chunk.chunk_count == 1 && slot.sig.width_type != PortWidthType::VL_W) { ++ri; continue; }
        os << "      case " << ri << ": {\n";
        if (slot.sig.width_type == PortWidthType::VL_W) {
          os << "        detail::apply_to_wide(decoders[idx], reinterpret_cast<uint32_t*>(&comb->" << slot.sig.name << "), " << slot.sig.array_size << ");\n";
        } else {
          os << "        uint64_t value = detail::assemble_scalar(decoders[idx]);\n";
          os << "        comb->" << slot.sig.name << " = static_cast<" << cpp_type_from_signal(slot.sig) << ">(value & detail::mask_bits(" << slot.sig.width << "));\n";
        }
        os << "        break;\n";
        os << "      }\n";
        ++ri;
      }
      os << "      default: break;\n";
      os << "      }\n";
      os << "    }\n";
    }
    os << "  }\n";
  }
  os << "}\n\n";

  // sendRemoteCOutputs
  os << "void CorvusSimWorkerGenP" << wp.pid << "::sendRemoteCOutputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  auto top_it = send_to_top.find(wp.pid);
  if (top_it != send_to_top.end() && !top_it->second.empty()) {
    os << "  const uint8_t slotBits = " << top_slot_bits << ";\n";
    for (const auto* slot : top_it->second) {
      os << "  {\n";
      os << "    CorvusBusEndpoint* ep = mBusEndpoints[" << slot->bus_index << "];\n";
      os << "    const uint32_t slotId = " << slot->slot_id << ";\n";
      os << "    const uint8_t dataBits = " << slot->chunk.data_bits << ";\n";
      os << "    const uint8_t chunkBits = " << slot->chunk.chunk_bits << ";\n";
      os << "    const uint32_t chunkCount = " << slot->chunk.chunk_count << ";\n";
      if (slot->sig.width_type == PortWidthType::VL_W) {
        os << "    const uint32_t* widePtr = reinterpret_cast<const uint32_t*>(&comb->" << slot->sig.name << ");\n";
        os << "    for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {\n";
        os << "      uint64_t chunkData = detail::read_wide_chunk(widePtr, " << slot->sig.array_size << ", dataBits, chunkIdx);\n";
        os << "      uint64_t payload = detail::pack_payload(slotId, chunkData, chunkIdx, slotBits, dataBits, chunkBits);\n";
        os << "      ep->send(0, payload);\n";
        os << "    }\n";
      } else {
        os << "    uint64_t value = static_cast<uint64_t>(comb->" << slot->sig.name << ");\n";
        os << "    for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {\n";
        os << "      uint64_t chunkData = (value >> (chunkIdx * dataBits)) & detail::mask_bits(dataBits);\n";
        os << "      uint64_t payload = detail::pack_payload(slotId, chunkData, chunkIdx, slotBits, dataBits, chunkBits);\n";
        os << "      ep->send(0, payload);\n";
        os << "    }\n";
      }
      os << "  }\n";
    }
  }
  os << "}\n\n";

  // loadSInputs (local C->S)
  os << "void CorvusSimWorkerGenP" << wp.pid << "::loadSInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!comb || !seq) return;\n";
  for (const auto& conn : wp.local_cts) {
    if (!conn.driver.port || conn.receivers.empty() || !conn.receivers[0].port) continue;
    std::string src = "comb->" + conn.driver.port->name;
    std::string dst = "seq->" + conn.receivers[0].port->name;
    if (conn.width_type == PortWidthType::VL_W) {
      int words = conn.driver.port ? conn.driver.port->array_size : conn.receivers[0].port->array_size;
      os << "  for (int i = 0; i < " << words << "; ++i) { " << dst << "[i] = " << src << "[i]; }\n";
    } else {
      os << "  " << dst << " = " << src << ";\n";
    }
  }
  os << "}\n\n";

  // sendRemoteSOutputs
  os << "void CorvusSimWorkerGenP" << wp.pid << "::sendRemoteSOutputs() {\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!seq) return;\n";
  auto remote_it = remote_send_map.find(wp.pid);
  if (remote_it != remote_send_map.end()) {
    for (const auto* slot : remote_it->second) {
      os << "  {\n";
      os << "    CorvusBusEndpoint* ep = sBusEndpoints[" << slot->bus_index << "];\n";
      os << "    const uint8_t slotBits = " << slot->slot_bits << ";\n";
      os << "    const uint32_t slotId = " << slot->slot_id << ";\n";
      os << "    const uint8_t dataBits = " << slot->chunk.data_bits << ";\n";
      os << "    const uint8_t chunkBits = " << slot->chunk.chunk_bits << ";\n";
      os << "    const uint32_t chunkCount = " << slot->chunk.chunk_count << ";\n";
      os << "    const uint32_t targetId = " << (slot->sig.receiver_pid + 1) << ";\n";
      if (slot->sig.width_type == PortWidthType::VL_W) {
        os << "    const uint32_t* widePtr = reinterpret_cast<const uint32_t*>(&seq->" << slot->sig.name << ");\n";
        os << "    for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {\n";
        os << "      uint64_t chunkData = detail::read_wide_chunk(widePtr, " << slot->sig.array_size << ", dataBits, chunkIdx);\n";
        os << "      uint64_t payload = detail::pack_payload(slotId, chunkData, chunkIdx, slotBits, dataBits, chunkBits);\n";
        os << "      ep->send(targetId, payload);\n";
        os << "    }\n";
      } else {
        os << "    uint64_t value = static_cast<uint64_t>(seq->" << slot->sig.name << ");\n";
        os << "    for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {\n";
        os << "      uint64_t chunkData = (value >> (chunkIdx * dataBits)) & detail::mask_bits(dataBits);\n";
        os << "      uint64_t payload = detail::pack_payload(slotId, chunkData, chunkIdx, slotBits, dataBits, chunkBits);\n";
        os << "      ep->send(targetId, payload);\n";
        os << "    }\n";
      }
      os << "  }\n";
    }
  }
  os << "}\n\n";

  // loadLocalCInputs (S -> C feedback)
  os << "void CorvusSimWorkerGenP" << wp.pid << "::loadLocalCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!comb || !seq) return;\n";
  for (const auto& conn : wp.local_stc) {
    if (!conn.driver.port || conn.receivers.empty() || !conn.receivers[0].port) continue;
    std::string src = "seq->" + conn.driver.port->name;
    std::string dst = "comb->" + conn.receivers[0].port->name;
    if (conn.width_type == PortWidthType::VL_W) {
      int words = conn.driver.port ? conn.driver.port->array_size : conn.receivers[0].port->array_size;
      os << "  for (int i = 0; i < " << words << "; ++i) { " << dst << "[i] = " << src << "[i]; }\n";
    } else {
      os << "  " << dst << " = " << src << ";\n";
    }
  }
  os << "}\n\n";

  os << "} // namespace corvus_generated\n";
  return os.str();
}

} // namespace

bool CorvusGenerator::generate(const ConnectionAnalysis& analysis,
                               const std::string& output_base,
                               int mbus_count,
                               int sbus_count) {
  std::string stage = "init";
  const int mbus_count_clamped = std::max(1, mbus_count);
  const int sbus_count_clamped = std::max(1, sbus_count);
  try {
    // Initialize worker plans from partitions
    std::map<int, WorkerPlan> workers;
    for (const auto& kv : analysis.partitions) {
      workers[kv.first].pid = kv.first;
    }
    auto get_worker = [&](int pid) -> WorkerPlan& {
      auto it = workers.find(pid);
      if (it == workers.end()) {
        it = workers.emplace(pid, WorkerPlan{}).first;
        it->second.pid = pid;
      }
      if (it->second.pid == -1) it->second.pid = pid;
      return it->second;
    };

    const ModuleInfo* external_mod = nullptr;
    std::map<std::string, SignalRef> top_inputs;
    std::map<std::string, SignalRef> top_outputs;
    std::vector<TopSlot> top_slots;

    stage = "collect_partitions";
    // Collect partition-local signals and register modules
    for (const auto& kv : analysis.partitions) {
      WorkerPlan& wp = get_worker(kv.first);
      for (const auto& conn : kv.second.local_cts_to_si) {
        wp.local_cts.push_back(conn);
        register_module(conn.driver, wp, external_mod);
        if (!conn.receivers.empty()) {
          register_module(conn.receivers[0], wp, external_mod);
        }
      }
      for (const auto& conn : kv.second.local_stc_to_ci) {
        wp.local_stc.push_back(conn);
        register_module(conn.driver, wp, external_mod);
        if (!conn.receivers.empty()) {
          register_module(conn.receivers[0], wp, external_mod);
        }
      }
      for (const auto& conn : kv.second.remote_s_to_c) {
        if (conn.receivers.empty()) continue;
        int target_pid = conn.receivers[0].module ? conn.receivers[0].module->partition_id : -1;
        WorkerPlan& target = get_worker(target_pid);
        register_module(conn.receivers[0], target, external_mod);
        register_module(conn.driver, wp, external_mod);
        RemoteRecvSlot slot;
        slot.sig = build_signal(conn, conn.receivers[0]);
        slot.from_pid = kv.first;
        target.remote_recv.push_back(slot);
      }
    }

    stage = "top_inputs_outputs";
    // Top inputs (I)
    for (const auto& conn : analysis.top_inputs) {
      if (!conn.receivers.empty()) {
        top_inputs.emplace(conn.port_name, build_signal(conn, conn.receivers[0]));
      }
      for (const auto& recv : conn.receivers) {
        int pid = recv.module ? recv.module->partition_id : -1;
        WorkerPlan& wp = get_worker(pid);
        register_module(recv, wp, external_mod);
        DownlinkSlot slot;
        slot.sig = build_signal(conn, recv);
        slot.from_external = false;
        wp.downlinks.push_back(slot);
      }
    }

    // External outputs (Eo) -> comb
    for (const auto& conn : analysis.external_outputs) {
      if (conn.driver.module && conn.driver.module->type == ModuleType::EXTERNAL) {
        external_mod = conn.driver.module;
      } else {
        register_module(conn.driver, get_worker(conn.driver.module ? conn.driver.module->partition_id : -1), external_mod);
      }
      for (const auto& recv : conn.receivers) {
        int pid = recv.module ? recv.module->partition_id : -1;
        WorkerPlan& wp = get_worker(pid);
        register_module(recv, wp, external_mod);
        DownlinkSlot slot;
        slot.sig = build_signal(conn, recv);
        slot.from_external = true;
        wp.downlinks.push_back(slot);
      }
    }

    // Top outputs (O) : comb -> top
    for (const auto& conn : analysis.top_outputs) {
      if (top_outputs.find(conn.port_name) == top_outputs.end()) {
        top_outputs.emplace(conn.port_name, build_signal(conn, conn.driver));
      }
      TopSlot slot;
      slot.sig = build_signal(conn, conn.driver);
      slot.to_external = false;
      top_slots.push_back(slot);
      if (conn.driver.module) {
        register_module(conn.driver, get_worker(conn.driver.module->partition_id), external_mod);
      }
    }

    // External inputs (Ei) : comb -> external
    for (const auto& conn : analysis.external_inputs) {
      if (!conn.receivers.empty() && conn.receivers[0].module) {
        external_mod = conn.receivers[0].module;
      }
      TopSlot slot;
      SignalEndpoint receiver = conn.receivers.empty() ? SignalEndpoint{} : conn.receivers[0];
      slot.sig = build_signal(conn, receiver);
      slot.to_external = true;
      top_slots.push_back(slot);
      if (conn.driver.module) {
        register_module(conn.driver, get_worker(conn.driver.module->partition_id), external_mod);
      }
    }

    stage = "slot_assignment";
    // Sort and assign slot metadata
    for (auto& kv : workers) {
      auto& wp = kv.second;
      std::sort(wp.downlinks.begin(), wp.downlinks.end(), [](const DownlinkSlot& a, const DownlinkSlot& b) {
        if (a.sig.name != b.sig.name) return a.sig.name < b.sig.name;
        return a.from_external < b.from_external;
      });
      wp.mbus_slot_bits = slot_bits_for_count(wp.downlinks.size());
      size_t downlink_ep = 0;
      for (size_t i = 0; i < wp.downlinks.size(); ++i) {
        wp.downlinks[i].slot_id = static_cast<int>(i);
        wp.downlinks[i].slot_bits = wp.mbus_slot_bits;
        wp.downlinks[i].chunk = compute_chunk_plan(wp.downlinks[i].sig.width, wp.downlinks[i].slot_bits);
        wp.downlinks[i].bus_index = static_cast<int>(downlink_ep % static_cast<size_t>(mbus_count_clamped));
        ++downlink_ep;
      }

      std::sort(wp.remote_recv.begin(), wp.remote_recv.end(), [](const RemoteRecvSlot& a, const RemoteRecvSlot& b) {
        if (a.from_pid != b.from_pid) return a.from_pid < b.from_pid;
        return a.sig.name < b.sig.name;
      });
      wp.remote_slot_bits = slot_bits_for_count(wp.remote_recv.size());
      size_t remote_ep = 0;
      for (size_t i = 0; i < wp.remote_recv.size(); ++i) {
        wp.remote_recv[i].slot_id = static_cast<int>(i);
        wp.remote_recv[i].slot_bits = wp.remote_slot_bits;
        wp.remote_recv[i].chunk = compute_chunk_plan(wp.remote_recv[i].sig.width, wp.remote_recv[i].slot_bits);
        wp.remote_recv[i].bus_index = static_cast<int>(remote_ep % static_cast<size_t>(sbus_count_clamped));
        ++remote_ep;
      }
    }

    std::sort(top_slots.begin(), top_slots.end(), [](const TopSlot& a, const TopSlot& b) {
      if (a.to_external != b.to_external) return a.to_external < b.to_external;
      if (a.sig.name != b.sig.name) return a.sig.name < b.sig.name;
      return a.sig.driver_pid < b.sig.driver_pid;
    });
    int top_slot_bits = slot_bits_for_count(top_slots.size());
    size_t top_ep = 0;
    for (size_t i = 0; i < top_slots.size(); ++i) {
      top_slots[i].slot_id = static_cast<int>(i);
      top_slots[i].slot_bits = top_slot_bits;
      top_slots[i].chunk = compute_chunk_plan(top_slots[i].sig.width, top_slots[i].slot_bits);
      top_slots[i].bus_index = static_cast<int>(top_ep % static_cast<size_t>(mbus_count_clamped));
      ++top_ep;
    }

    std::map<int, std::vector<const TopSlot*>> send_to_top;
    for (const auto& slot : top_slots) {
      if (slot.sig.driver_pid >= 0) {
        send_to_top[slot.sig.driver_pid].push_back(&slot);
      }
    }

    std::map<int, std::vector<const RemoteRecvSlot*>> remote_send_map;
    for (const auto& kv : workers) {
      for (const auto& slot : kv.second.remote_recv) {
        if (slot.from_pid >= 0) {
          remote_send_map[slot.from_pid].push_back(&slot);
        }
      }
    }

    stage = "write_json";
    // Write JSON artifact
    std::string json_path = output_base + "_corvus.json";
    std::ofstream ofs(json_path);
    if (!ofs.is_open()) {
      std::cerr << "Failed to open output: " << json_path << std::endl;
      return false;
    }

    ofs << "{\n";
    ofs << "  \"top_inputs\": ";
    write_connections(ofs, analysis.top_inputs);
    ofs << ",\n  \"top_outputs\": ";
    write_connections(ofs, analysis.top_outputs);
    ofs << ",\n  \"external_inputs\": ";
    write_connections(ofs, analysis.external_inputs);
    ofs << ",\n  \"external_outputs\": ";
    write_connections(ofs, analysis.external_outputs);
    ofs << ",\n  \"partitions\": {";

    size_t idx = 0;
    for (const auto& kv : analysis.partitions) {
      ofs << "\n    \"" << kv.first << "\": {";
      ofs << "\"local_cts_to_si\": ";
      write_connections(ofs, kv.second.local_cts_to_si);
      ofs << ", \"local_stc_to_ci\": ";
      write_connections(ofs, kv.second.local_stc_to_ci);
      ofs << ", \"remote_s_to_c\": ";
      write_connections(ofs, kv.second.remote_s_to_c);
      ofs << "}";
      if (++idx < analysis.partitions.size()) ofs << ",";
    }
    ofs << "\n  },\n";

    ofs << "  \"warnings\": [";
    for (size_t i = 0; i < analysis.warnings.size(); ++i) {
      ofs << "\"" << analysis.warnings[i] << "\"";
      if (i + 1 < analysis.warnings.size()) ofs << ", ";
    }
    ofs << "]\n";
    ofs << "}\n";
    ofs.close();
    std::cout << "Corvus generator wrote: " << json_path << std::endl;

    stage = "write_top_worker";
    // Top header/cpp
    std::string top_header_path = output_base + "_corvus_top.h";
    std::string top_cpp_path = output_base + "_corvus_top.cpp";
    {
      std::ofstream th(top_header_path);
      if (!th.is_open()) {
        std::cerr << "Failed to open output: " << top_header_path << std::endl;
        return false;
      }
      th << generate_top_header(output_base, external_mod, top_inputs, top_outputs,
                                mbus_count_clamped, sbus_count_clamped);
    }
    {
      std::ofstream tc(top_cpp_path);
      if (!tc.is_open()) {
        std::cerr << "Failed to open output: " << top_cpp_path << std::endl;
        return false;
      }
      tc << generate_top_cpp(output_base, external_mod,
                             workers, top_slots, top_slot_bits, mbus_count_clamped, sbus_count_clamped);
    }

    // Worker headers/cpps
    std::vector<std::string> worker_headers;
    for (const auto& kv : workers) {
      const auto& wp = kv.second;
      if (!wp.comb || !wp.seq) continue;
      std::string w_header_path = output_base + "_corvus_worker_p" + std::to_string(kv.first) + ".h";
      std::string w_cpp_path = output_base + "_corvus_worker_p" + std::to_string(kv.first) + ".cpp";
      worker_headers.push_back(w_header_path);
      std::set<std::string> worker_headers_set;
      if (wp.comb) worker_headers_set.insert(wp.comb->header_path);
      if (wp.seq) worker_headers_set.insert(wp.seq->header_path);
      {
        std::ofstream wh(w_header_path);
        if (!wh.is_open()) {
        std::cerr << "Failed to open output: " << w_header_path << std::endl;
        return false;
      }
      wh << generate_worker_header(output_base, wp, mbus_count_clamped, sbus_count_clamped, worker_headers_set);
    }
      {
        std::ofstream wc(w_cpp_path);
        if (!wc.is_open()) {
          std::cerr << "Failed to open output: " << w_cpp_path << std::endl;
          return false;
        }
        wc << generate_worker_cpp(output_base, wp, mbus_count_clamped, sbus_count_clamped, top_slot_bits,
                                  send_to_top, remote_send_map, worker_headers_set);
      }
    }

    stage = "write_aggregate";
    std::string agg_path = output_base + "_corvus_gen.h";
    {
      std::ofstream agg(agg_path);
      if (!agg.is_open()) {
        std::cerr << "Failed to open output: " << agg_path << std::endl;
        return false;
      }
      std::string guard = sanitize_guard(output_base + "_AGG");
      agg << "#ifndef " << guard << "\n";
      agg << "#define " << guard << "\n\n";
      agg << "#include \"" << path_basename(output_base + "_corvus_top.h") << "\"\n";
      for (const auto& wh : worker_headers) {
        agg << "#include \"" << path_basename(wh) << "\"\n";
      }
      agg << "\n#endif // " << guard << "\n";
    }

    std::cout << "Corvus generator wrote: " << top_header_path << " and " << top_cpp_path << std::endl;
    std::cout << "Corvus generator wrote " << worker_headers.size() << " worker header/cpp pairs\n";
    std::cout << "Corvus generator wrote: " << agg_path << std::endl;
    return true;
  } catch (const std::exception& e) {
    std::cerr << "CorvusGenerator::generate failed at stage '" << stage << "': " << e.what() << std::endl;
    return false;
  }
}
