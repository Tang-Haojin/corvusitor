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

struct SlotSlice {
  int slot_id = 0;
  int bit_offset = 0;
};

struct DownlinkSlot {
  SignalRef sig;
  bool from_external = false;
  int slot_id = 0;
  int slot_bits = 32;
  int bus_index = 0;
  std::vector<SlotSlice> slices;
};

struct TopSlot {
  SignalRef sig;
  bool to_external = false;
  int slot_id = 0;
  int slot_bits = 32;
  int bus_index = 0;
  std::vector<SlotSlice> slices;
};

struct RemoteRecvSlot {
  SignalRef sig;
  int from_pid = -1;
  int slot_id = 0;
  int slot_bits = 32;
  int bus_index = 0;
  std::vector<SlotSlice> slices;
};

struct WorkerPlan {
  int pid = -1;
  const ModuleInfo* comb = nullptr;
  const ModuleInfo* seq = nullptr;
  std::vector<DownlinkSlot> downlinks;
  std::vector<RemoteRecvSlot> remote_recv;
  std::vector<ClassifiedConnection> local_cts;
  std::vector<ClassifiedConnection> local_stc;
  int mbus_slot_bits = 32;
  int remote_slot_bits = 32;
};

std::vector<SlotSlice> build_slot_slices(const SignalRef& sig, int base_slot) {
  int slices = slice_count_for_width(sig.width);
  std::vector<SlotSlice> slice_map;
  slice_map.reserve(static_cast<size_t>(slices));
  for (int i = 0; i < slices; ++i) {
    SlotSlice slice;
    slice.slot_id = base_slot + i;
    slice.bit_offset = i * 16;
    slice_map.push_back(slice);
  }
  return slice_map;
}

class SlotAddressSpace {
public:
  explicit SlotAddressSpace(int bus_count)
      : bus_count_(std::max(1, bus_count)) {}

  template <typename SlotContainer>
  void assign_slots(SlotContainer& slots) {
    for (auto& slot : slots) {
      int base = next_slot_;
      slot.slot_id = base;
      slot.slices = build_slot_slices(slot.sig, base);
      next_slot_ += static_cast<int>(slot.slices.size());
      slot.slot_bits = slot_bits_for_count(static_cast<size_t>(next_slot_));
      slot.bus_index = bus_cursor_;
      bus_cursor_ = (bus_cursor_ + 1) % bus_count_;
    }
  }

  int total_slots() const { return next_slot_; }
  int slot_bits() const { return slot_bits_for_count(static_cast<size_t>(next_slot_)); }

private:
  int next_slot_ = 0;
  int bus_cursor_ = 0;
  int bus_count_ = 1;
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
                             const ModuleInfo* external_mod,
                             const std::map<int, WorkerPlan>& workers,
                             const std::vector<TopSlot>& top_slots,
                             int top_slot_bits,
                             int mbus_count,
                             int sbus_count) {
  const std::string top_class = top_class_name(output_base);
  const std::string top_header_name = top_class + ".h";
  std::set<std::string> module_headers;
  if (external_mod) module_headers.insert(external_mod->header_path);
  (void)mbus_count;
  (void)sbus_count;
  (void)workers;

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
  os << "  assert(this->mBusEndpoints.size() == kCorvusGenMBusCount && \"MBus endpoint count mismatch\");\n";
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

  // MBus communication temporarily disabled; TODO: reintroduce once sync strategy settles
  os << "void " << top_class << "::sendIAndEOutput() {\n";
  os << "  // TODO: implement MBus downlink once protocol is finalized.\n";
  os << "}\n\n";

  os << "void " << top_class << "::loadOAndEInput() {\n";
  os << "  auto* ports = static_cast<" << top_class << "::TopPortsGen*>(topPorts);\n";
  os << "  (void)ports;\n";
  if (!top_slots.empty()) {
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
    for (const auto& slot : top_slots) {
      const auto& sig = slot.sig;
      const std::string data_mask_hex = "0xFFFFULL";
      const std::string dst_expr = slot.to_external
        ? (std::string("ext->") + (sig.receiver.port ? sig.receiver.port->name : sig.name))
        : (std::string("ports->") + sig.name);
      const std::string dst_guard = slot.to_external ? "ext" : "ports";
        for (const auto& slice : slot.slices) {
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
                                   int mbus_count,
                                   int sbus_count,
                                   const std::set<std::string>& module_headers) {
  std::ostringstream os;
  std::string guard = sanitize_guard(output_base + "_WORKER_P" + std::to_string(wp.pid));
  std::string counts_guard = sanitize_guard(output_base + "_COUNTS");
  os << "#ifndef " << guard << "\n";
  os << "#define " << guard << "\n\n";
  write_includes(os, module_headers);
  os << "namespace corvus_generated {\n\n";
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
                                int mbus_count,
                                int sbus_count,
                                int top_slot_bits,
                                const std::map<int, std::vector<const TopSlot*>>& send_to_top,
                                const std::map<int, std::vector<const RemoteRecvSlot*>>& remote_send_map,
                                const std::set<std::string>& module_headers) {
  std::ostringstream os;
  const std::string worker_class = worker_class_name(output_base, wp.pid);
  os << "#include \"" << path_basename(worker_class + ".h") << "\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\nnamespace corvus_generated {\n\n";
  (void)mbus_count;
  (void)sbus_count;
  (void)top_slot_bits;
  (void)remote_send_map;

  os << worker_class << "::" << worker_class << "(\n";
  os << "    CorvusSimWorkerSynctreeEndpoint* simWorkerSynctreeEndpoint,\n";
  os << "    std::vector<CorvusBusEndpoint*> mBusEndpoints,\n";
  os << "    std::vector<CorvusBusEndpoint*> sBusEndpoints)\n";
  os << "    : CorvusSimWorker(simWorkerSynctreeEndpoint, std::move(mBusEndpoints), std::move(sBusEndpoints)) {\n";
  os << "  setName(\"" << worker_class << "\");\n";
  os << "  assert(this->mBusEndpoints.size() == kCorvusGenMBusCount && \"MBus endpoint count mismatch\");\n";
  os << "  assert(this->sBusEndpoints.size() == kCorvusGenSBusCount && \"SBus endpoint count mismatch\");\n";
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
  os << "  // TODO: MBus receive path disabled during refactor.\n";
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
      os << "    // slot " << sig.name << " (base slotId=" << slot->slot_id << ")\n";
      if (sig.width_type == PortWidthType::VL_W) {
        for (const auto& slice : slot->slices) {
          os << "    {\n";
          os << "      int bitOffset = " << slice.bit_offset << ";\n";
          os << "      int word = bitOffset / 32;\n";
          os << "      int wordBit = bitOffset % 32;\n";
          os << "      slice_data = static_cast<uint64_t>(" << src << "[word]);\n";
          os << "      slice_data >>= wordBit;\n";
          os << "      slice_data &= " << data_mask_hex << ";\n";
          os << "      payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
          os << "      payload |= (slice_data << 32);\n";
          os << "      mBusEndpoints[" << slot->bus_index << "]->send(0, payload);\n";
          os << "    }\n";
        }
      } else {
        os << "    uint64_t src_val = static_cast<uint64_t>(" << src << ");\n";
        for (const auto& slice : slot->slices) {
          os << "    {\n";
          os << "      int bitOffset = " << slice.bit_offset << ";\n";
          os << "      slice_data = (src_val >> bitOffset) & " << data_mask_hex << ";\n";
          os << "      payload = static_cast<uint64_t>(" << slice.slot_id << ");\n";
          os << "      payload |= (slice_data << 32);\n";
          os << "      mBusEndpoints[" << slot->bus_index << "]->send(0, payload);\n";
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
  os << "  // TODO: SBus send path disabled during refactor.\n";
  os << "}\n\n";

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
      for (const auto& conn : kv.second.local_c_to_s) {
        wp.local_cts.push_back(conn);
        register_module(conn.driver, wp, external_mod);
        if (!conn.receivers.empty()) {
          register_module(conn.receivers[0], wp, external_mod);
        }
      }
      for (const auto& conn : kv.second.local_s_to_c) {
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
      SlotAddressSpace downlink_space(mbus_count_clamped);
      downlink_space.assign_slots(wp.downlinks);
      wp.mbus_slot_bits = downlink_space.slot_bits();

      std::sort(wp.remote_recv.begin(), wp.remote_recv.end(), [](const RemoteRecvSlot& a, const RemoteRecvSlot& b) {
        if (a.from_pid != b.from_pid) return a.from_pid < b.from_pid;
        return a.sig.name < b.sig.name;
      });
      SlotAddressSpace remote_space(sbus_count_clamped);
      remote_space.assign_slots(wp.remote_recv);
      wp.remote_slot_bits = remote_space.slot_bits();
    }

    std::sort(top_slots.begin(), top_slots.end(), [](const TopSlot& a, const TopSlot& b) {
      if (a.to_external != b.to_external) return a.to_external < b.to_external;
      if (a.sig.name != b.sig.name) return a.sig.name < b.sig.name;
      return a.sig.driver_pid < b.sig.driver_pid;
    });
    SlotAddressSpace top_space(mbus_count_clamped);
    top_space.assign_slots(top_slots);
    int top_slot_bits = top_space.slot_bits(); // fixed slotId width per protocol

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
      ofs << "\"local_c_to_s\": ";
      write_connections(ofs, kv.second.local_c_to_s);
      ofs << ", \"local_s_to_c\": ";
      write_connections(ofs, kv.second.local_s_to_c);
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
  const std::string output_dir = path_dirname(output_base);
  const std::string top_class = top_class_name(output_base);
  const std::string top_header_file = top_class + ".h";
  const std::string top_cpp_file = top_class + ".cpp";
  // Top header/cpp
  std::string top_header_path = path_join(output_dir, top_header_file);
  std::string top_cpp_path = path_join(output_dir, top_cpp_file);
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
