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

using SignalRef = CorvusGenerator::SignalRef;
using SlotRecord = CorvusGenerator::SlotRecord;
using RecvSignal = CorvusGenerator::RecvSignal;
using RecvPlan = CorvusGenerator::RecvPlan;
using WorkerPlan = CorvusGenerator::WorkerPlan;
using AddressPlan = CorvusGenerator::AddressPlan;

int slot_bits_for_count(size_t count) {
  // Protocol: fixed 48-bit frame: [47:32]=slotData(16-bit), [31:0]=slotId(32-bit)
  (void)count;
  return 32;
}

int slice_count_for_width(int width) {
  int slices = (width + 15) / 16;
  return slices > 0 ? slices : 1;
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

std::vector<SlotRecord> build_slot_records(const SignalRef& sig, int base_slot) {
  int slices = slice_count_for_width(sig.width);
  std::vector<SlotRecord> slice_map;
  slice_map.reserve(static_cast<size_t>(slices));
  for (int i = 0; i < slices; ++i) {
    SlotRecord slice;
    slice.slot_id = base_slot + i;
    slice.bit_offset = i * 16;
    slice_map.push_back(slice);
  }
  return slice_map;
}

[[maybe_unused]] void write_endpoint(std::ostream& os, const SignalEndpoint& ep) {
  os << "{ \"module\": \"" << (ep.module ? ep.module->instance_name : "null")
     << "\", \"port\": \"" << (ep.port ? ep.port->name : "null") << "\" }";
}

[[maybe_unused]] void write_connections(std::ostream& os, const std::vector<ClassifiedConnection>& conns) {
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

std::string path_dirname(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  if (pos == 0) return path.substr(0, 1);
  return path.substr(0, pos);
}

std::string path_join(const std::string& dir, const std::string& file) {
  if (dir.empty() || dir == ".") return file;
  char sep = (dir.find('\\') != std::string::npos) ? '\\' : '/';
  if (dir.back() == '/' || dir.back() == '\\') {
    return dir + file;
  }
  return dir + sep + file;
}

std::string sanitize_identifier(const std::string& base) {
  std::string id;
  id.reserve(base.size());
  for (char c : base) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      id.push_back(c);
    } else {
      id.push_back('_');
    }
  }
  if (id.empty()) id = "output";
  if (std::isdigit(static_cast<unsigned char>(id.front()))) {
    id.insert(id.begin(), '_');
  }
  return id;
}

std::string output_token(const std::string& output_base) {
  return sanitize_identifier(path_basename(output_base));
}

std::string top_class_name(const std::string& output_base) {
  return "C" + output_token(output_base) + "TopModuleGen";
}

std::string worker_class_name(const std::string& output_base, int pid) {
  return "C" + output_token(output_base) + "SimWorkerGenP" + std::to_string(pid);
}

std::string aggregate_header_name(const std::string& output_base) {
  return "C" + output_token(output_base) + "CorvusGen.h";
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
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\n";
}

std::string generate_top_header(const std::string& output_base,
                                const AddressPlan& plan) {
  const ModuleInfo* external_mod = plan.external_mod;
  const auto& top_inputs = plan.top_inputs;
  const auto& top_outputs = plan.top_outputs;
  const size_t mbus_count = static_cast<size_t>(std::max(1, plan.mbus_endpoint_count));
  const size_t sbus_count = static_cast<size_t>(std::max(1, plan.sbus_endpoint_count));
  std::set<std::string> module_headers;
  if (external_mod) module_headers.insert(external_mod->header_path);

  std::ostringstream os;
  std::string guard = sanitize_guard(output_base + "_TOP");
  os << "#ifndef " << guard << "\n";
  os << "#define " << guard << "\n\n";
  write_includes(os, module_headers);
  os << "namespace corvus_generated {\n\n";
  std::string counts_guard = sanitize_guard(output_base + "_COUNTS");
  os << "#ifndef " << counts_guard << "\n";
  os << "#define " << counts_guard << "\n";
  os << "inline constexpr size_t kCorvusGenMBusCount = " << mbus_count << ";\n";
  os << "inline constexpr size_t kCorvusGenSBusCount = " << sbus_count << ";\n";
  os << "#endif\n\n";

  std::string top_class = top_class_name(output_base);
  std::string ext_class = external_mod ? external_mod->class_name : "";
  os << "class " << top_class << " : public CorvusTopModule {\n";
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
  os << "  " << top_class << "(CorvusTopSynctreeEndpoint* topSynctreeEndpoint,\n";
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
                             const AddressPlan& plan) {
  const ModuleInfo* external_mod = plan.external_mod;
  const auto& workers = plan.workers;
  const RecvPlan& top_plan = plan.top_recv_plan;
  const std::string top_class = top_class_name(output_base);
  const std::string top_header_name = top_class + ".h";
  std::set<std::string> module_headers;
  if (external_mod) module_headers.insert(external_mod->header_path);

  std::ostringstream os;
  os << "#include \"" << path_basename(top_header_name) << "\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\nnamespace corvus_generated {\n\n";
  std::string ext_class = external_mod ? external_mod->class_name : "";

  os << top_class << "::" << top_class << "(CorvusTopSynctreeEndpoint* topSynctreeEndpoint,\n";
  os << "                                     std::vector<CorvusBusEndpoint*> mBusEndpoints)\n";
  os << "    : CorvusTopModule(topSynctreeEndpoint, std::move(mBusEndpoints)) {\n";
  os << "  assert(this->mBusEndpoints.size() >= kCorvusGenMBusCount && \"MBus endpoint count insufficient\");\n";
  os << "}\n\n";

  os << "TopPorts* " << top_class << "::createTopPorts() { return new TopPortsGen(); }\n";
  os << "void " << top_class << "::deleteTopPorts() {\n";
  os << "  delete static_cast<" << top_class << "::TopPortsGen*>(topPorts);\n";
  os << "  topPorts = nullptr;\n";
  os << "}\n";
  if (!ext_class.empty()) {
    os << "ModuleHandle* " << top_class << "::createExternalModule() {\n";
    os << "  auto* inst = new " << ext_class << "();\n";
    os << "  return new VerilatorModuleHandle<" << ext_class << ">(inst);\n";
    os << "}\n";
    os << "void " << top_class << "::deleteExternalModule() {\n";
    os << "  auto* handle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eModule);\n";
    os << "  if (handle) {\n";
    os << "    delete handle->mp;\n";
    os << "    delete handle;\n";
    os << "    eModule = nullptr;\n";
    os << "  }\n";
    os << "}\n";
  } else {
    os << "ModuleHandle* " << top_class << "::createExternalModule() { return nullptr; }\n";
    os << "void " << top_class << "::deleteExternalModule() { eModule = nullptr; }\n";
  }

  os << "void " << top_class << "::sendIAndEOutput() {\n";
  os << "  auto* ports = static_cast<" << top_class << "::TopPortsGen*>(topPorts);\n";
  os << "  (void)ports;\n";
  bool has_downlinks = false;
  for (const auto& kv : workers) {
    for (const auto& sig : kv.second.recv_plan.signals) {
      if (!sig.via_sbus) { has_downlinks = true; break; }
    }
    if (has_downlinks) break;
  }
  if (has_downlinks) {
    if (external_mod) {
      os << "  auto* eHandle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eModule);\n";
      os << "  auto* ext = eHandle ? eHandle->mp : nullptr;\n";
      os << "  (void)ext;\n";
    } else {
      os << "  (void)eModule;\n";
    }
    os << "  uint64_t payload = 0;\n";
    os << "  uint64_t slice_data = 0;\n";
    for (const auto& kv : workers) {
      const auto& wp = kv.second;
      const auto& recv_plan = wp.recv_plan;
      bool worker_has_downlink = false;
      for (const auto& sig : recv_plan.signals) {
        if (!sig.via_sbus) { worker_has_downlink = true; break; }
      }
      if (!worker_has_downlink) continue;
      os << "  {\n";
      os << "    const uint32_t targetId = " << (wp.pid + 1) << ";\n";
      for (const auto& slot : recv_plan.signals) {
        if (slot.via_sbus) continue;
        const auto& sig = slot.sig;
        const std::string data_mask_hex = "0xFFFFULL";
        const std::string src_expr = slot.from_external
          ? (std::string("ext->") + (sig.driver.port ? sig.driver.port->name : sig.name))
          : (std::string("ports->") + sig.name);
        os << "    {\n";
        os << "      // slot " << sig.name << " (base slotId=" << (slot.slots.empty() ? 0 : slot.slots.front().slot_id) << ")\n";
        if (sig.width_type == PortWidthType::VL_W) {
          for (const auto& slice : slot.slots) {
            os << "      {\n";
            os << "        int bitOffset = " << slice.bit_offset << ";\n";
            os << "        int word = bitOffset / 32;\n";
            os << "        int wordBit = bitOffset % 32;\n";
            os << "        slice_data = static_cast<uint64_t>(" << src_expr << "[word]);\n";
            os << "        slice_data >>= wordBit;\n";
            os << "        slice_data &= " << data_mask_hex << ";\n";
            os << "        payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
            os << "        payload |= (slice_data << 32);\n";
            os << "        mBusEndpoints[0]->send(targetId, payload);\n";
            os << "      }\n";
          }
        } else {
          os << "      uint64_t src_val = static_cast<uint64_t>(" << src_expr << ");\n";
          for (const auto& slice : slot.slots) {
            os << "      {\n";
            os << "        int bitOffset = " << slice.bit_offset << ";\n";
            os << "        slice_data = (src_val >> bitOffset) & " << data_mask_hex << ";\n";
            os << "        payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
            os << "        payload |= (slice_data << 32);\n";
            os << "        mBusEndpoints[0]->send(targetId, payload);\n";
            os << "      }\n";
          }
        }
        os << "    }\n";
      }
      os << "  }\n";
    }
  }
  os << "}\n\n";

  os << "void " << top_class << "::loadOAndEInput() {\n";
  os << "  auto* ports = static_cast<" << top_class << "::TopPortsGen*>(topPorts);\n";
  os << "  (void)ports;\n";
  if (!top_plan.signals.empty()) {
    const std::string slot_mask_hex = "0xFFFFFFFFULL";
    if (external_mod) {
      os << "  auto* eHandle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eModule);\n";
      os << "  auto* ext = eHandle ? eHandle->mp : nullptr;\n";
      os << "  (void)ext;\n";
    } else {
      os << "  (void)eModule;\n";
    }
    os << "  const uint64_t kSlotMask = " << slot_mask_hex << ";\n";
    os << "  for (size_t ep = 0; ep < mBusEndpoints.size(); ++ep) {\n";
    os << "    int cnt = mBusEndpoints[ep]->bufferCnt();\n";
    os << "    for (int i = 0; i < cnt; ++i) {\n";
    os << "      uint64_t payload = mBusEndpoints[ep]->recv();\n";
    os << "      uint32_t slotId = static_cast<uint32_t>(payload & kSlotMask);\n";
    os << "      switch (slotId) {\n";
    for (const auto& slot : top_plan.signals) {
      const auto& sig = slot.sig;
      const std::string data_mask_hex = "0xFFFFULL";
      const std::string dst_expr = slot.to_external
        ? (std::string("ext->") + (sig.receiver.port ? sig.receiver.port->name : sig.name))
        : (std::string("ports->") + sig.name);
      const std::string dst_guard = slot.to_external ? "ext" : "ports";
        for (const auto& slice : slot.slots) {
          os << "      case " << slice.slot_id << ": {\n";
          os << "        uint64_t data = (payload >> 32) & " << data_mask_hex << ";\n";
          os << "        int bitOffset = " << slice.bit_offset << ";\n";
        if (sig.width_type == PortWidthType::VL_W) {
          os << "        int word = bitOffset / 32;\n";
          os << "        int wordBit = bitOffset % 32;\n";
          os << "        if (" << dst_guard << ") {\n";
          os << "          uint32_t lowMask = static_cast<uint32_t>(" << data_mask_hex << " << wordBit);\n";
          os << "          uint32_t lowBits = static_cast<uint32_t>(data << wordBit);\n";
          os << "          " << dst_expr << "[word] = (" << dst_expr << "[word] & ~lowMask) | lowBits;\n";
          os << "        }\n";
        } else if (sig.width <= 16) {
          os << "        if (" << dst_guard << ") {\n";
          os << "          " << dst_expr << " = static_cast<" << cpp_type_from_signal(sig) << ">(data);\n";
          os << "        }\n";
        } else {
          os << "        uint64_t cur = static_cast<uint64_t>(" << dst_expr << ");\n";
          os << "        uint64_t mask = (" << data_mask_hex << ") << bitOffset;\n";
          os << "        cur = (cur & ~mask) | ((data & " << data_mask_hex << ") << bitOffset);\n";
          os << "        " << dst_expr << " = static_cast<" << cpp_type_from_signal(sig) << ">(cur);\n";
        }
          os << "        break;\n";
          os << "      }\n";
      }
    }
    os << "      default: break;\n";
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
  }
  os << "}\n\n";

  os << "} // namespace corvus_generated\n";
  return os.str();
}

std::string generate_worker_header(const std::string& output_base,
                                   const WorkerPlan& wp,
                                   const AddressPlan& plan,
                                   const std::set<std::string>& module_headers) {
  std::ostringstream os;
  std::string guard = sanitize_guard(output_base + "_WORKER_P" + std::to_string(wp.pid));
  std::string counts_guard = sanitize_guard(output_base + "_COUNTS");
  os << "#ifndef " << guard << "\n";
  os << "#define " << guard << "\n\n";
  write_includes(os, module_headers);
  os << "namespace corvus_generated {\n\n";
  const size_t mbus_count = static_cast<size_t>(std::max(1, plan.mbus_endpoint_count));
  const size_t sbus_count = static_cast<size_t>(std::max(1, plan.sbus_endpoint_count));
  os << "#ifndef " << counts_guard << "\n";
  os << "#define " << counts_guard << "\n";
  os << "inline constexpr size_t kCorvusGenMBusCount = " << mbus_count << ";\n";
  os << "inline constexpr size_t kCorvusGenSBusCount = " << sbus_count << ";\n";
  os << "#endif\n\n";

  std::string worker_class = worker_class_name(output_base, wp.pid);
  os << "class " << worker_class << " : public CorvusSimWorker {\n";
  os << "public:\n";
  os << "  " << worker_class << "(CorvusSimWorkerSynctreeEndpoint* simWorkerSynctreeEndpoint,\n";
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
                                const AddressPlan& plan,
                                const std::map<int, std::vector<const RecvSignal*>>& send_to_top,
                                const std::map<int, std::vector<const RecvSignal*>>& remote_send_map,
                                const std::set<std::string>& module_headers) {
  std::ostringstream os;
  const std::string worker_class = worker_class_name(output_base, wp.pid);
  os << "#include \"" << path_basename(worker_class + ".h") << "\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\nnamespace corvus_generated {\n\n";
  (void)plan;

  os << worker_class << "::" << worker_class << "(\n";
  os << "    CorvusSimWorkerSynctreeEndpoint* simWorkerSynctreeEndpoint,\n";
  os << "    std::vector<CorvusBusEndpoint*> mBusEndpoints,\n";
  os << "    std::vector<CorvusBusEndpoint*> sBusEndpoints)\n";
  os << "    : CorvusSimWorker(simWorkerSynctreeEndpoint, std::move(mBusEndpoints), std::move(sBusEndpoints)) {\n";
  os << "  setName(\"" << worker_class << "\");\n";
  os << "  assert(this->mBusEndpoints.size() >= kCorvusGenMBusCount && \"MBus endpoint count insufficient\");\n";
  os << "  assert(this->sBusEndpoints.size() >= kCorvusGenSBusCount && \"SBus endpoint count insufficient\");\n";
  os << "}\n\n";

  os << "void " << worker_class << "::createSimModules() {\n";
  os << "  cModule = new VerilatorModuleHandle<" << wp.comb->class_name << ">(new " << wp.comb->class_name << "());\n";
  os << "  sModule = new VerilatorModuleHandle<" << wp.seq->class_name << ">(new " << wp.seq->class_name << "());\n";
  os << "}\n";
  os << "void " << worker_class << "::deleteSimModules() {\n";
  os << "  auto* cHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  if (cHandle) { delete cHandle->mp; delete cHandle; }\n";
  os << "  auto* sHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  if (sHandle) { delete sHandle->mp; delete sHandle; }\n";
  os << "  cModule = nullptr; sModule = nullptr;\n";
  os << "}\n\n";

  // loadRemoteCInputs
  os << "void " << worker_class << "::loadRemoteCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  os << "  const uint64_t kSlotMask = 0xFFFFFFFFULL;\n";
  bool has_downlink_slots = false;
  bool has_remote_slots = false;
  for (const auto& sig : wp.recv_plan.signals) {
    if (sig.via_sbus) {
      has_remote_slots = true;
    } else {
      has_downlink_slots = true;
    }
  }
  if (!has_downlink_slots && !has_remote_slots) {
    os << "  (void)kSlotMask;\n";
  }
  if (has_downlink_slots) {
    os << "  for (size_t ep = 0; ep < mBusEndpoints.size(); ++ep) {\n";
    os << "    int cnt = mBusEndpoints[ep]->bufferCnt();\n";
    os << "    for (int i = 0; i < cnt; ++i) {\n";
    os << "      uint64_t payload = mBusEndpoints[ep]->recv();\n";
    os << "      uint32_t slotId = static_cast<uint32_t>(payload & kSlotMask);\n";
    os << "      switch (slotId) {\n";
    for (const auto& slot : wp.recv_plan.signals) {
      if (slot.via_sbus) continue;
      const auto& sig = slot.sig;
      const std::string data_mask_hex = "0xFFFFULL";
      const std::string dst_expr = std::string("comb->") + (sig.receiver.port ? sig.receiver.port->name : sig.name);
      for (const auto& slice : slot.slots) {
        os << "      case " << slice.slot_id << ": {\n";
        os << "        uint64_t data = (payload >> 32) & " << data_mask_hex << ";\n";
        os << "        int bitOffset = " << slice.bit_offset << ";\n";
        if (sig.width_type == PortWidthType::VL_W) {
          os << "        int word = bitOffset / 32;\n";
          os << "        int wordBit = bitOffset % 32;\n";
          os << "        uint32_t lowMask = static_cast<uint32_t>(" << data_mask_hex << " << wordBit);\n";
          os << "        uint32_t lowBits = static_cast<uint32_t>(data << wordBit);\n";
          os << "        " << dst_expr << "[word] = (" << dst_expr << "[word] & ~lowMask) | lowBits;\n";
        } else if (sig.width <= 16) {
          os << "        " << dst_expr << " = static_cast<" << cpp_type_from_signal(sig) << ">(data);\n";
        } else {
          os << "        uint64_t cur = static_cast<uint64_t>(" << dst_expr << ");\n";
          os << "        uint64_t mask = (" << data_mask_hex << ") << bitOffset;\n";
          os << "        cur = (cur & ~mask) | ((data & " << data_mask_hex << ") << bitOffset);\n";
          os << "        " << dst_expr << " = static_cast<" << cpp_type_from_signal(sig) << ">(cur);\n";
        }
        os << "        break;\n";
        os << "      }\n";
      }
    }
    os << "      default: break;\n";
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
  }

  if (has_remote_slots) {
    os << "  for (size_t ep = 0; ep < sBusEndpoints.size(); ++ep) {\n";
    os << "    int cnt = sBusEndpoints[ep]->bufferCnt();\n";
    os << "    for (int i = 0; i < cnt; ++i) {\n";
    os << "      uint64_t payload = sBusEndpoints[ep]->recv();\n";
    os << "      uint32_t slotId = static_cast<uint32_t>(payload & kSlotMask);\n";
    os << "      switch (slotId) {\n";
    for (const auto& slot : wp.recv_plan.signals) {
      if (!slot.via_sbus) continue;
      const auto& sig = slot.sig;
      const std::string data_mask_hex = "0xFFFFULL";
      const std::string dst_expr = std::string("comb->") + (sig.receiver.port ? sig.receiver.port->name : sig.name);
      for (const auto& slice : slot.slots) {
        os << "      case " << slice.slot_id << ": {\n";
        os << "        uint64_t data = (payload >> 32) & " << data_mask_hex << ";\n";
        os << "        int bitOffset = " << slice.bit_offset << ";\n";
        if (sig.width_type == PortWidthType::VL_W) {
          os << "        int word = bitOffset / 32;\n";
          os << "        int wordBit = bitOffset % 32;\n";
          os << "        uint32_t lowMask = static_cast<uint32_t>(" << data_mask_hex << " << wordBit);\n";
          os << "        uint32_t lowBits = static_cast<uint32_t>(data << wordBit);\n";
          os << "        " << dst_expr << "[word] = (" << dst_expr << "[word] & ~lowMask) | lowBits;\n";
        } else if (sig.width <= 16) {
          os << "        " << dst_expr << " = static_cast<" << cpp_type_from_signal(sig) << ">(data);\n";
        } else {
          os << "        uint64_t cur = static_cast<uint64_t>(" << dst_expr << ");\n";
          os << "        uint64_t mask = (" << data_mask_hex << ") << bitOffset;\n";
          os << "        cur = (cur & ~mask) | ((data & " << data_mask_hex << ") << bitOffset);\n";
          os << "        " << dst_expr << " = static_cast<" << cpp_type_from_signal(sig) << ">(cur);\n";
        }
        os << "        break;\n";
        os << "      }\n";
      }
    }
    os << "      default: break;\n";
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
  }

  os << "}\n\n";

  // sendRemoteCOutputs
  os << "void " << worker_class << "::sendRemoteCOutputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  os << "  uint64_t payload = 0;\n";
  os << "  uint64_t slice_data = 0;\n";
  auto send_it = send_to_top.find(wp.pid);
  if (send_it == send_to_top.end() || send_it->second.empty()) {
    os << "  (void)payload; (void)slice_data;\n";
    os << "}\n\n";
  } else {
    for (const auto* slot : send_it->second) {
      const auto& sig = slot->sig;
      std::string src = "comb->" + (sig.driver.port ? sig.driver.port->name : sig.name);
      const std::string data_mask_hex = "0xFFFFULL";
      os << "  {\n";
      os << "    // slot " << sig.name << " (base slotId=" << (slot->slots.empty() ? 0 : slot->slots.front().slot_id) << ")\n";
      if (sig.width_type == PortWidthType::VL_W) {
        for (const auto& slice : slot->slots) {
          os << "    {\n";
          os << "      int bitOffset = " << slice.bit_offset << ";\n";
          os << "      int word = bitOffset / 32;\n";
          os << "      int wordBit = bitOffset % 32;\n";
          os << "      slice_data = static_cast<uint64_t>(" << src << "[word]);\n";
          os << "      slice_data >>= wordBit;\n";
          os << "      slice_data &= " << data_mask_hex << ";\n";
          os << "      payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
          os << "      payload |= (slice_data << 32);\n";
          os << "      mBusEndpoints[0]->send(0, payload);\n";
          os << "    }\n";
        }
      } else {
        os << "    uint64_t src_val = static_cast<uint64_t>(" << src << ");\n";
        for (const auto& slice : slot->slots) {
          os << "    {\n";
          os << "      int bitOffset = " << slice.bit_offset << ";\n";
          os << "      slice_data = (src_val >> bitOffset) & " << data_mask_hex << ";\n";
          os << "      payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
          os << "      payload |= (slice_data << 32);\n";
          os << "      mBusEndpoints[0]->send(0, payload);\n";
          os << "    }\n";
        }
      }
      os << "  }\n";
    }
    os << "}\n\n";
  }

  // loadSInputs (local C->S)
  os << "void " << worker_class << "::loadSInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!comb || !seq) return;\n";
  for (const auto& conn : wp.local_cts) {
    if (!conn.driver.port) continue;
    std::string src = "comb->" + conn.driver.port->name;
    for (const auto& recv : conn.receivers) {
      if (!recv.port) continue;
      std::string dst = "seq->" + recv.port->name;
      if (conn.width_type == PortWidthType::VL_W) {
        int words = recv.port ? recv.port->array_size : conn.driver.port->array_size;
        os << "  for (int i = 0; i < " << words << "; ++i) { " << dst << "[i] = " << src << "[i]; }\n";
      } else {
        os << "  " << dst << " = " << src << ";\n";
      }
    }
  }
  os << "}\n\n";

  // sendRemoteSOutputs
  os << "void " << worker_class << "::sendRemoteSOutputs() {\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!seq) return;\n";
  os << "  uint64_t payload = 0;\n";
  os << "  uint64_t slice_data = 0;\n";
  auto remote_it = remote_send_map.find(wp.pid);
  if (remote_it == remote_send_map.end() || remote_it->second.empty()) {
    os << "  (void)payload; (void)slice_data;\n";
    os << "}\n\n";
  } else {
    for (const auto* slot : remote_it->second) {
      const auto& sig = slot->sig;
      std::string src = "seq->" + (sig.driver.port ? sig.driver.port->name : sig.name);
      const std::string data_mask_hex = "0xFFFFULL";
      os << "  {\n";
      os << "    // slot " << sig.name << " -> P" << sig.receiver_pid << " (base slotId=" << (slot->slots.empty() ? 0 : slot->slots.front().slot_id) << ")\n";
      os << "    const uint32_t targetId = " << (sig.receiver_pid + 1) << ";\n";
      if (sig.width_type == PortWidthType::VL_W) {
        for (const auto& slice : slot->slots) {
          os << "    {\n";
          os << "      int bitOffset = " << slice.bit_offset << ";\n";
          os << "      int word = bitOffset / 32;\n";
          os << "      int wordBit = bitOffset % 32;\n";
          os << "      slice_data = static_cast<uint64_t>(" << src << "[word]);\n";
          os << "      slice_data >>= wordBit;\n";
          os << "      slice_data &= " << data_mask_hex << ";\n";
          os << "      payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
          os << "      payload |= (slice_data << 32);\n";
          os << "      sBusEndpoints[0]->send(targetId, payload);\n";
          os << "    }\n";
        }
      } else {
        os << "    uint64_t src_val = static_cast<uint64_t>(" << src << ");\n";
        for (const auto& slice : slot->slots) {
          os << "    {\n";
          os << "      int bitOffset = " << slice.bit_offset << ";\n";
          os << "      slice_data = (src_val >> bitOffset) & " << data_mask_hex << ";\n";
          os << "      payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
          os << "      payload |= (slice_data << 32);\n";
          os << "      sBusEndpoints[0]->send(targetId, payload);\n";
          os << "    }\n";
        }
      }
      os << "  }\n";
    }
    os << "}\n\n";
  }

  // loadLocalCInputs (S -> C feedback)
  os << "void " << worker_class << "::loadLocalCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!comb || !seq) return;\n";
  for (const auto& conn : wp.local_stc) {
    if (!conn.driver.port) continue;
    std::string src = "seq->" + conn.driver.port->name;
    for (const auto& recv : conn.receivers) {
      if (!recv.port) continue;
      std::string dst = "comb->" + recv.port->name;
      if (conn.width_type == PortWidthType::VL_W) {
        int words = recv.port ? recv.port->array_size : conn.driver.port->array_size;
        os << "  for (int i = 0; i < " << words << "; ++i) { " << dst << "[i] = " << src << "[i]; }\n";
      } else {
        os << "  " << dst << " = " << src << ";\n";
      }
    }
  }
  os << "}\n\n";

  os << "} // namespace corvus_generated\n";
  return os.str();
}

} // namespace

void CorvusGenerator::SlotAddressSpace::assign_slots_impl(std::vector<RecvSignal>& slots) {
  for (auto& sig : slots) {
    const int base = next_slot_;
    sig.slots = build_slot_records(sig.sig, base);
    next_slot_ += static_cast<int>(sig.slots.size());
  }
}

void CorvusGenerator::SlotAddressSpace::assign_slots(std::vector<RecvSignal>& slots) {
  assign_slots_impl(slots);
}

int CorvusGenerator::SlotAddressSpace::slot_bits() const {
  return slot_bits_for_count(static_cast<size_t>(next_slot_));
}

CorvusGenerator::AddressPlan CorvusGenerator::build_address_plan(const ConnectionAnalysis& analysis) const {
  AddressPlan plan;
  plan.top_recv_plan.pid = -1;
  auto get_worker = [&](int pid) -> WorkerPlan& {
    auto it = plan.workers.find(pid);
    if (it == plan.workers.end()) {
      WorkerPlan wp;
      wp.pid = pid;
      wp.recv_plan.pid = pid;
      it = plan.workers.emplace(pid, std::move(wp)).first;
    }
    if (it->second.pid == -1) it->second.pid = pid;
    if (it->second.recv_plan.pid == -1) it->second.recv_plan.pid = pid;
    return it->second;
  };

  auto add_signal = [&](WorkerPlan& wp,
                        const ClassifiedConnection& conn,
                        const SignalEndpoint& recv,
                        bool from_external,
                        bool via_sbus) {
    RecvSignal sig;
    sig.sig = build_signal(conn, recv);
    sig.from_external = from_external;
    sig.via_sbus = via_sbus;
    wp.recv_plan.signals.push_back(std::move(sig));
  };

  // Collect partition-local signals and register modules
  for (const auto& kv : analysis.partitions) {
    WorkerPlan& wp = get_worker(kv.first);
    for (const auto& conn : kv.second.local_c_to_s) {
      wp.local_cts.push_back(conn);
      register_module(conn.driver, wp, plan.external_mod);
      if (!conn.receivers.empty()) {
        register_module(conn.receivers[0], wp, plan.external_mod);
      }
    }
    for (const auto& conn : kv.second.local_s_to_c) {
      wp.local_stc.push_back(conn);
      register_module(conn.driver, wp, plan.external_mod);
      if (!conn.receivers.empty()) {
        register_module(conn.receivers[0], wp, plan.external_mod);
      }
    }
    for (const auto& conn : kv.second.remote_s_to_c) {
      if (conn.receivers.empty()) continue;
      int target_pid = conn.receivers[0].module ? conn.receivers[0].module->partition_id : -1;
      WorkerPlan& target = get_worker(target_pid);
      register_module(conn.receivers[0], target, plan.external_mod);
      register_module(conn.driver, wp, plan.external_mod);
      add_signal(target, conn, conn.receivers[0], false, true);
    }
  }

  // Top inputs (I)
  for (const auto& conn : analysis.top_inputs) {
    if (!conn.receivers.empty()) {
      plan.top_inputs.emplace(conn.port_name, build_signal(conn, conn.receivers[0]));
    }
    for (const auto& recv : conn.receivers) {
      int pid = recv.module ? recv.module->partition_id : -1;
      WorkerPlan& wp = get_worker(pid);
      register_module(recv, wp, plan.external_mod);
      add_signal(wp, conn, recv, false, false);
    }
  }

  // External outputs (Eo) -> comb
  for (const auto& conn : analysis.external_outputs) {
    if (conn.driver.module && conn.driver.module->type == ModuleType::EXTERNAL) {
      plan.external_mod = conn.driver.module;
    } else {
      register_module(conn.driver, get_worker(conn.driver.module ? conn.driver.module->partition_id : -1), plan.external_mod);
    }
    for (const auto& recv : conn.receivers) {
      int pid = recv.module ? recv.module->partition_id : -1;
      WorkerPlan& wp = get_worker(pid);
      register_module(recv, wp, plan.external_mod);
      add_signal(wp, conn, recv, true, false);
    }
  }

  // Top outputs (O) : comb -> top
  for (const auto& conn : analysis.top_outputs) {
    if (plan.top_outputs.find(conn.port_name) == plan.top_outputs.end()) {
      plan.top_outputs.emplace(conn.port_name, build_signal(conn, conn.driver));
    }
    RecvSignal slot;
    slot.sig = build_signal(conn, conn.driver);
    slot.to_external = false;
    plan.top_recv_plan.signals.push_back(std::move(slot));
    if (conn.driver.module) {
      register_module(conn.driver, get_worker(conn.driver.module->partition_id), plan.external_mod);
    }
  }

  // External inputs (Ei) : comb -> external
  for (const auto& conn : analysis.external_inputs) {
    if (!conn.receivers.empty() && conn.receivers[0].module) {
      plan.external_mod = conn.receivers[0].module;
    }
    SignalEndpoint receiver = conn.receivers.empty() ? SignalEndpoint{} : conn.receivers[0];
    RecvSignal slot;
    slot.sig = build_signal(conn, receiver);
    slot.to_external = true;
    plan.top_recv_plan.signals.push_back(std::move(slot));
    if (conn.driver.module) {
      register_module(conn.driver, get_worker(conn.driver.module->partition_id), plan.external_mod);
    }
  }

  auto sort_signals = [](std::vector<RecvSignal>& signals) {
    std::sort(signals.begin(), signals.end(), [](const RecvSignal& a, const RecvSignal& b) {
      if (a.via_sbus != b.via_sbus) return a.via_sbus < b.via_sbus;
      if (a.to_external != b.to_external) return a.to_external < b.to_external;
      if (a.from_external != b.from_external) return a.from_external < b.from_external;
      if (a.sig.name != b.sig.name) return a.sig.name < b.sig.name;
      return a.sig.driver_pid < b.sig.driver_pid;
    });
  };

  // Sort and assign slot metadata
  for (auto& kv : plan.workers) {
    auto& wp = kv.second;
    sort_signals(wp.recv_plan.signals);
    SlotAddressSpace recv_space;
    recv_space.assign_slots(wp.recv_plan.signals);
    wp.recv_plan.slot_bits = recv_space.slot_bits();

    bool has_mbus_slots = false;
    bool has_sbus_slots = false;
    for (const auto& sig : wp.recv_plan.signals) {
      if (sig.via_sbus) {
        has_sbus_slots = true;
      } else {
        has_mbus_slots = true;
      }
    }
    if (has_mbus_slots) {
      plan.mbus_endpoint_count = std::max(plan.mbus_endpoint_count, recv_space.required_bus_count());
    }
    if (has_sbus_slots) {
      plan.sbus_endpoint_count = std::max(plan.sbus_endpoint_count, recv_space.required_bus_count());
    }
  }

  sort_signals(plan.top_recv_plan.signals);
  SlotAddressSpace top_space;
  top_space.assign_slots(plan.top_recv_plan.signals);
  plan.top_recv_plan.slot_bits = top_space.slot_bits();
  if (!plan.top_recv_plan.signals.empty()) {
    plan.mbus_endpoint_count = std::max(plan.mbus_endpoint_count, top_space.required_bus_count());
  }

  return plan;
}

bool CorvusGenerator::write_plan_json(const ConnectionAnalysis& analysis,
                                      const AddressPlan& plan,
                                      const std::string& output_base) const {
  const std::string json_path = output_base + "_corvus.json";
  std::ofstream ofs(json_path);
  if (!ofs.is_open()) {
    std::cerr << "Failed to open output: " << json_path << std::endl;
    return false;
  }

  auto write_slots = [&](const RecvSignal& sig) {
    ofs << "[";
    for (size_t i = 0; i < sig.slots.size(); ++i) {
      const auto& slice = sig.slots[i];
      ofs << "{ \"slotId\": " << slice.slot_id << ", \"bitOffset\": " << slice.bit_offset << "}";
      if (i + 1 < sig.slots.size()) ofs << ", ";
    }
    ofs << "]";
  };

  auto write_recv_plan = [&](const RecvPlan& rp) {
    ofs << "{ \"pid\": " << rp.pid << ", \"signals\": [";
    for (size_t i = 0; i < rp.signals.size(); ++i) {
      const auto& sig = rp.signals[i];
      ofs << "\n      { \"name\": \"" << sig.sig.name << "\", \"width\": " << sig.sig.width
          << ", \"width_type\": " << static_cast<int>(sig.sig.width_type)
          << ", \"from_external\": " << (sig.from_external ? "true" : "false")
          << ", \"to_external\": " << (sig.to_external ? "true" : "false")
          << ", \"via_sbus\": " << (sig.via_sbus ? "true" : "false")
          << ", \"slots\": ";
      write_slots(sig);
      ofs << " }";
      if (i + 1 < rp.signals.size()) ofs << ",";
    }
    ofs << (rp.signals.empty() ? "" : "\n    ") << "] }";
  };

  ofs << "{\n";
  ofs << "  \"warnings\": [";
  for (size_t i = 0; i < analysis.warnings.size(); ++i) {
    ofs << "\"" << analysis.warnings[i] << "\"";
    if (i + 1 < analysis.warnings.size()) ofs << ", ";
  }
  ofs << "],\n";

  ofs << "  \"recv_plans\": {";
  ofs << "\n    \"" << plan.top_recv_plan.pid << "\": ";
  write_recv_plan(plan.top_recv_plan);
  for (auto it = plan.workers.begin(); it != plan.workers.end(); ++it) {
    ofs << ",\n    \"" << it->first << "\": ";
    write_recv_plan(it->second.recv_plan);
  }
  ofs << "\n  }\n";
  ofs << "}\n";
  ofs.close();
  std::cout << "Corvus generator wrote: " << json_path << std::endl;
  return true;
}

bool CorvusGenerator::generate(const ConnectionAnalysis& analysis,
                               const std::string& output_base,
                               int mbus_count,
                               int sbus_count) {
  std::string stage = "init";
  try {
    stage = "build_address_plan";
    AddressPlan plan = build_address_plan(analysis);
    plan.mbus_endpoint_count = std::max(1, plan.mbus_endpoint_count);
    plan.sbus_endpoint_count = std::max(1, plan.sbus_endpoint_count);
    if (mbus_count > 0 && mbus_count < plan.mbus_endpoint_count) {
      std::cerr << "Requested mbus-count " << mbus_count << " < required endpoints " << plan.mbus_endpoint_count << "\n";
      return false;
    }
    if (sbus_count > 0 && sbus_count < plan.sbus_endpoint_count) {
      std::cerr << "Requested sbus-count " << sbus_count << " < required endpoints " << plan.sbus_endpoint_count << "\n";
      return false;
    }

    stage = "write_json";
    if (!write_plan_json(analysis, plan, output_base)) {
      return false;
    }

    std::map<int, std::vector<const RecvSignal*>> send_to_top;
    for (const auto& sig : plan.top_recv_plan.signals) {
      if (sig.sig.driver_pid >= 0) {
        send_to_top[sig.sig.driver_pid].push_back(&sig);
      }
    }

    std::map<int, std::vector<const RecvSignal*>> remote_send_map;
    for (const auto& kv : plan.workers) {
      for (const auto& sig : kv.second.recv_plan.signals) {
        if (sig.via_sbus && sig.sig.driver_pid >= 0) {
          remote_send_map[sig.sig.driver_pid].push_back(&sig);
        }
      }
    }

    stage = "write_top_worker";
    const std::string output_dir = path_dirname(output_base);
    const std::string top_class = top_class_name(output_base);
    const std::string top_header_file = top_class + ".h";
    const std::string top_cpp_file = top_class + ".cpp";
    std::string top_header_path = path_join(output_dir, top_header_file);
    std::string top_cpp_path = path_join(output_dir, top_cpp_file);
    {
      std::ofstream th(top_header_path);
      if (!th.is_open()) {
        std::cerr << "Failed to open output: " << top_header_path << std::endl;
        return false;
      }
      th << generate_top_header(output_base, plan);
    }
    {
      std::ofstream tc(top_cpp_path);
      if (!tc.is_open()) {
        std::cerr << "Failed to open output: " << top_cpp_path << std::endl;
        return false;
      }
      tc << generate_top_cpp(output_base, plan);
    }

    // Worker headers/cpps
    std::vector<std::string> worker_headers;
    for (const auto& kv : plan.workers) {
      const auto& wp = kv.second;
      if (!wp.comb || !wp.seq) continue;
      const std::string worker_class = worker_class_name(output_base, kv.first);
      std::string w_header_path = path_join(output_dir, worker_class + ".h");
      std::string w_cpp_path = path_join(output_dir, worker_class + ".cpp");
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
        wh << generate_worker_header(output_base, wp, plan, worker_headers_set);
      }
      {
        std::ofstream wc(w_cpp_path);
        if (!wc.is_open()) {
          std::cerr << "Failed to open output: " << w_cpp_path << std::endl;
          return false;
        }
        wc << generate_worker_cpp(output_base, wp, plan, send_to_top, remote_send_map, worker_headers_set);
      }
    }

    stage = "write_aggregate";
    std::string agg_header = aggregate_header_name(output_base);
    std::string agg_path = path_join(output_dir, agg_header);
    {
      std::ofstream agg(agg_path);
      if (!agg.is_open()) {
        std::cerr << "Failed to open output: " << agg_path << std::endl;
        return false;
      }
      std::string guard = sanitize_guard(output_base + "_AGG");
      agg << "#ifndef " << guard << "\n";
      agg << "#define " << guard << "\n\n";
      agg << "#include \"" << path_basename(top_header_file) << "\"\n";
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
