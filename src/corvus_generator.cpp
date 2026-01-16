#include "corvus_generator.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using SlotRecvRecord = CorvusGenerator::SlotRecvRecord;
using SlotSendRecord = CorvusGenerator::SlotSendRecord;
using CopyRecord = CorvusGenerator::CopyRecord;
using TopModulePlan = CorvusGenerator::TopModulePlan;
using SimWorkerPlan = CorvusGenerator::SimWorkerPlan;
using CorvusBusPlan = CorvusGenerator::CorvusBusPlan;

struct SignalRef {
  std::string name;
  int width = 0;
  PortWidthType width_type = PortWidthType::VL_8;
  int array_size = 0;
  SignalEndpoint driver;
  SignalEndpoint receiver;
};

struct SlotRecvMeta {
  SlotRecvRecord record;
  int width = 0;
  PortWidthType width_type = PortWidthType::VL_8;
  int array_size = 0;
  const ModuleInfo* driver_module = nullptr;
  const PortInfo* driver_port = nullptr;
  const ModuleInfo* receiver_module = nullptr;
  const PortInfo* receiver_port = nullptr;
  bool via_sbus = false;
  bool from_external = false;
  bool to_external = false;
};

struct SlotSendMeta {
  SlotSendRecord record;
  int width = 0;
  PortWidthType width_type = PortWidthType::VL_8;
  int array_size = 0;
  const ModuleInfo* driver_module = nullptr;
  const PortInfo* driver_port = nullptr;
  bool from_external = false;
  bool to_external = false;
};

struct CopyMeta {
  CopyRecord record;
  const ModuleInfo* driver_module = nullptr;
  const PortInfo* driver_port = nullptr;
  const ModuleInfo* receiver_module = nullptr;
  const PortInfo* receiver_port = nullptr;
  int width = 0;
  PortWidthType width_type = PortWidthType::VL_8;
  int array_size = 0;
};

struct WorkerGenPlan {
  int pid = -1;
  int next_slot = 0;
  const ModuleInfo* comb = nullptr;
  const ModuleInfo* seq = nullptr;
  std::vector<SlotRecvMeta> mbus_recvs;
  std::vector<SlotRecvMeta> sbus_recvs;
  std::vector<SlotSendMeta> send_to_top;
  std::vector<SlotSendMeta> send_remote;
  std::vector<CopyMeta> copy_cts;
  std::vector<CopyMeta> copy_stc;
};

struct TopGenPlan {
  int next_slot = 0;
  const ModuleInfo* external = nullptr;
  std::map<std::string, SignalRef> top_inputs;
  std::map<std::string, SignalRef> top_outputs;
  std::vector<SlotSendMeta> send_inputs;
  std::vector<SlotSendMeta> send_external_outputs;
  std::vector<SlotRecvMeta> recv_outputs;
  std::vector<SlotRecvMeta> recv_external_inputs;
};

struct GenerationPlan {
  CorvusBusPlan bus_plan;
  TopGenPlan top;
  std::map<int, WorkerGenPlan> workers;
  std::vector<std::string> warnings;
  int mbus_count = 1;
  int sbus_count = 1;
};

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
  if (conn.driver.port && conn.driver.port->width_type == PortWidthType::VL_W) {
    ref.array_size = conn.driver.port->array_size;
  } else {
    ref.array_size = array_size_from_endpoint(receiver, conn.width_type);
  }
  return ref;
}

std::string width_type_to_string(PortWidthType t) {
  switch (t) {
  case PortWidthType::VL_8: return "VL_8";
  case PortWidthType::VL_16: return "VL_16";
  case PortWidthType::VL_32: return "VL_32";
  case PortWidthType::VL_64: return "VL_64";
  case PortWidthType::VL_W: return "VL_W";
  }
  return "UNKNOWN";
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

WorkerGenPlan& ensure_worker_plan(int pid, GenerationPlan& plan) {
  auto it = plan.workers.find(pid);
  if (it == plan.workers.end()) {
    WorkerGenPlan wp;
    wp.pid = pid;
    it = plan.workers.emplace(pid, std::move(wp)).first;
  }
  plan.bus_plan.simWorkerPlans[pid];
  return it->second;
}

void register_module(const SignalEndpoint& ep, WorkerGenPlan& wp, TopGenPlan& top_plan) {
  if (!ep.module) return;
  switch (ep.module->type) {
  case ModuleType::COMB:
    if (!wp.comb) wp.comb = ep.module;
    break;
  case ModuleType::SEQ:
    if (!wp.seq) wp.seq = ep.module;
    break;
  case ModuleType::EXTERNAL:
    if (!top_plan.external) top_plan.external = ep.module;
    break;
  }
}

std::string cpp_type_from_meta(const SlotRecvMeta& meta) {
  if (meta.receiver_port) return meta.receiver_port->get_cpp_type();
  if (meta.driver_port) return meta.driver_port->get_cpp_type();
  return cpp_type_from_endpoint({}, meta.width_type, meta.array_size);
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

void sort_bus_plan(CorvusBusPlan& plan) {
  auto sort_send = [](std::vector<SlotSendRecord>& v) {
    std::sort(v.begin(), v.end(), [](const SlotSendRecord& a, const SlotSendRecord& b) {
      if (a.targetId != b.targetId) return a.targetId < b.targetId;
      if (a.slotId != b.slotId) return a.slotId < b.slotId;
      if (a.portName != b.portName) return a.portName < b.portName;
      return a.bitOffset < b.bitOffset;
    });
  };
  auto sort_recv = [](std::vector<SlotRecvRecord>& v) {
    std::sort(v.begin(), v.end(), [](const SlotRecvRecord& a, const SlotRecvRecord& b) {
      if (a.slotId != b.slotId) return a.slotId < b.slotId;
      if (a.portName != b.portName) return a.portName < b.portName;
      return a.bitOffset < b.bitOffset;
    });
  };

  sort_send(plan.topModulePlan.input);
  sort_send(plan.topModulePlan.externalOutput);
  sort_recv(plan.topModulePlan.output);
  sort_recv(plan.topModulePlan.externalInput);

  for (auto& kv : plan.simWorkerPlans) {
    sort_recv(kv.second.loadMBusCInputs);
    sort_recv(kv.second.loadSBusCInputs);
    sort_send(kv.second.sendMBusCOutputs);
    sort_send(kv.second.sendSBusSOutputs);
    std::sort(kv.second.copySInputs.begin(), kv.second.copySInputs.end(),
              [](const CopyRecord& a, const CopyRecord& b) { return a.portName < b.portName; });
    std::sort(kv.second.copyLocalCInputs.begin(), kv.second.copyLocalCInputs.end(),
              [](const CopyRecord& a, const CopyRecord& b) { return a.portName < b.portName; });
  }
}

template <typename T>
void sort_meta(std::vector<T>& v) {
  std::sort(v.begin(), v.end(), [](const T& a, const T& b) {
    if constexpr (std::is_same<T, SlotSendMeta>::value) {
      if (a.record.targetId != b.record.targetId) return a.record.targetId < b.record.targetId;
      if (a.record.slotId != b.record.slotId) return a.record.slotId < b.record.slotId;
      if (a.record.portName != b.record.portName) return a.record.portName < b.record.portName;
      return a.record.bitOffset < b.record.bitOffset;
    } else {
      if (a.record.slotId != b.record.slotId) return a.record.slotId < b.record.slotId;
      if (a.record.portName != b.record.portName) return a.record.portName < b.record.portName;
      return a.record.bitOffset < b.record.bitOffset;
    }
  });
}

GenerationPlan build_generation_plan(const ConnectionAnalysis& analysis,
                                     int mbus_count,
                                     int sbus_count) {
  GenerationPlan gen;
  gen.warnings = analysis.warnings;
  gen.mbus_count = std::max(1, mbus_count);
  gen.sbus_count = std::max(1, sbus_count);

  auto add_top_slot = [&](int width, PortWidthType width_type, const SignalEndpoint& driver,
                          const SignalEndpoint& receiver, bool to_external,
                          std::vector<SlotRecvMeta>& metas, std::vector<SlotRecvRecord>& plan_vec) {
    int slices = slice_count_for_width(width);
    for (int i = 0; i < slices; ++i) {
      SlotRecvRecord recv;
      recv.portName = receiver.port ? receiver.port->name : driver.port ? driver.port->name : "";
      recv.slotId = gen.top.next_slot++;
      recv.bitOffset = i * 16;
      SlotRecvMeta meta;
      meta.record = recv;
      meta.width = width;
      meta.width_type = width_type;
      meta.driver_module = driver.module;
      meta.driver_port = driver.port;
      meta.receiver_module = receiver.module;
      meta.receiver_port = receiver.port;
      meta.to_external = to_external;
      meta.array_size = (receiver.port ? receiver.port->array_size : array_size_from_endpoint(driver, width_type));
      metas.push_back(meta);
      plan_vec.push_back(recv);
    }
  };

  // Top inputs (I) -> workers (MBus)
  for (const auto& conn : analysis.top_inputs) {
    if (conn.receivers.empty()) continue;
    // Record top IO type info
    if (gen.top.top_inputs.find(conn.port_name) == gen.top.top_inputs.end()) {
      gen.top.top_inputs.emplace(conn.port_name, build_signal(conn, conn.receivers.front()));
    }
    for (const auto& recv : conn.receivers) {
      int pid = recv.module ? recv.module->partition_id : -1;
      auto& wp = ensure_worker_plan(pid, gen);
      register_module(recv, wp, gen.top);
      int slices = slice_count_for_width(conn.width);
      for (int i = 0; i < slices; ++i) {
        SlotRecvRecord recv_rec;
        recv_rec.portName = recv.port ? recv.port->name : conn.port_name;
        recv_rec.slotId = wp.next_slot++;
        recv_rec.bitOffset = i * 16;

        SlotRecvMeta recv_meta;
        recv_meta.record = recv_rec;
        recv_meta.width = conn.width;
        recv_meta.width_type = conn.width_type;
        recv_meta.receiver_module = recv.module;
        recv_meta.receiver_port = recv.port;
        recv_meta.array_size = array_size_from_endpoint(recv, conn.width_type);

        gen.bus_plan.simWorkerPlans[pid].loadMBusCInputs.push_back(recv_rec);
        wp.mbus_recvs.push_back(recv_meta);

        SlotSendRecord send_rec;
        send_rec.portName = conn.port_name;
        send_rec.bitOffset = i * 16;
        send_rec.targetId = pid + 1;
        send_rec.slotId = recv_rec.slotId;

        SlotSendMeta send_meta;
        send_meta.record = send_rec;
        send_meta.width = conn.width;
        send_meta.width_type = conn.width_type;
        send_meta.array_size = recv_meta.array_size;

        gen.bus_plan.topModulePlan.input.push_back(send_rec);
        gen.top.send_inputs.push_back(send_meta);
      }
    }
  }

  // External outputs (Eo) -> comb (MBus)
  for (const auto& conn : analysis.external_outputs) {
    if (conn.receivers.empty()) continue;
    for (const auto& recv : conn.receivers) {
      int pid = recv.module ? recv.module->partition_id : -1;
      auto& wp = ensure_worker_plan(pid, gen);
      register_module(recv, wp, gen.top);
      register_module(conn.driver, wp, gen.top);
      int slices = slice_count_for_width(conn.width);
      for (int i = 0; i < slices; ++i) {
        SlotRecvRecord recv_rec;
        recv_rec.portName = recv.port ? recv.port->name : conn.port_name;
        recv_rec.slotId = wp.next_slot++;
        recv_rec.bitOffset = i * 16;

        SlotRecvMeta recv_meta;
        recv_meta.record = recv_rec;
        recv_meta.width = conn.width;
        recv_meta.width_type = conn.width_type;
        recv_meta.receiver_module = recv.module;
        recv_meta.receiver_port = recv.port;
        recv_meta.driver_module = conn.driver.module;
        recv_meta.driver_port = conn.driver.port;
        recv_meta.from_external = true;
        recv_meta.array_size = conn.driver.port ? conn.driver.port->array_size : array_size_from_endpoint(recv, conn.width_type);

        gen.bus_plan.simWorkerPlans[pid].loadMBusCInputs.push_back(recv_rec);
        wp.mbus_recvs.push_back(recv_meta);

        SlotSendRecord send_rec;
        send_rec.portName = conn.driver.port ? conn.driver.port->name : conn.port_name;
        send_rec.bitOffset = i * 16;
        send_rec.targetId = pid + 1;
        send_rec.slotId = recv_rec.slotId;

        SlotSendMeta send_meta;
        send_meta.record = send_rec;
        send_meta.width = conn.width;
        send_meta.width_type = conn.width_type;
        send_meta.driver_module = conn.driver.module;
        send_meta.driver_port = conn.driver.port;
        send_meta.from_external = true;
        send_meta.array_size = recv_meta.array_size;

        gen.bus_plan.topModulePlan.externalOutput.push_back(send_rec);
        gen.top.send_external_outputs.push_back(send_meta);
      }
    }
  }

  // Remote S->C (SBus)
  for (const auto& kv : analysis.partitions) {
    int src_pid = kv.first;
    for (const auto& conn : kv.second.remote_s_to_c) {
      if (conn.receivers.empty()) continue;
      const auto& recv = conn.receivers.front();
      int dst_pid = recv.module ? recv.module->partition_id : -1;
      auto& src_wp = ensure_worker_plan(src_pid, gen);
      auto& dst_wp = ensure_worker_plan(dst_pid, gen);
      register_module(conn.driver, src_wp, gen.top);
      register_module(recv, dst_wp, gen.top);
      int slices = slice_count_for_width(conn.width);
      for (int i = 0; i < slices; ++i) {
        SlotRecvRecord recv_rec;
        recv_rec.portName = recv.port ? recv.port->name : conn.port_name;
        recv_rec.slotId = dst_wp.next_slot++;
        recv_rec.bitOffset = i * 16;

        SlotRecvMeta recv_meta;
        recv_meta.record = recv_rec;
        recv_meta.width = conn.width;
        recv_meta.width_type = conn.width_type;
        recv_meta.driver_module = conn.driver.module;
        recv_meta.driver_port = conn.driver.port;
        recv_meta.receiver_module = recv.module;
        recv_meta.receiver_port = recv.port;
        recv_meta.via_sbus = true;
        recv_meta.array_size = conn.driver.port ? conn.driver.port->array_size : array_size_from_endpoint(recv, conn.width_type);

        gen.bus_plan.simWorkerPlans[dst_pid].loadSBusCInputs.push_back(recv_rec);
        dst_wp.sbus_recvs.push_back(recv_meta);

        SlotSendRecord send_rec;
        send_rec.portName = conn.driver.port ? conn.driver.port->name : conn.port_name;
        send_rec.bitOffset = i * 16;
        send_rec.targetId = dst_pid + 1;
        send_rec.slotId = recv_rec.slotId;

        SlotSendMeta send_meta;
        send_meta.record = send_rec;
        send_meta.width = conn.width;
        send_meta.width_type = conn.width_type;
        send_meta.driver_module = conn.driver.module;
        send_meta.driver_port = conn.driver.port;
        send_meta.array_size = recv_meta.array_size;

        gen.bus_plan.simWorkerPlans[src_pid].sendSBusSOutputs.push_back(send_rec);
        src_wp.send_remote.push_back(send_meta);
      }
    }
  }

  // Top outputs (O) : comb -> top (MBus)
  for (const auto& conn : analysis.top_outputs) {
    if (gen.top.top_outputs.find(conn.port_name) == gen.top.top_outputs.end()) {
      gen.top.top_outputs.emplace(conn.port_name, build_signal(conn, conn.driver));
    }
    SignalEndpoint receiver; // top
    add_top_slot(conn.width, conn.width_type, conn.driver, receiver, false,
                 gen.top.recv_outputs, gen.bus_plan.topModulePlan.output);
    if (conn.driver.module) {
      int pid = conn.driver.module->partition_id;
      auto& wp = ensure_worker_plan(pid, gen);
      register_module(conn.driver, wp, gen.top);
      int slices = slice_count_for_width(conn.width);
      for (int i = 0; i < slices; ++i) {
        const int slot_id = gen.top.next_slot - slices + i;
        SlotSendRecord send_rec;
        send_rec.portName = conn.driver.port ? conn.driver.port->name : conn.port_name;
        send_rec.bitOffset = i * 16;
        send_rec.targetId = 0;
        send_rec.slotId = slot_id;

        SlotSendMeta send_meta;
        send_meta.record = send_rec;
        send_meta.width = conn.width;
        send_meta.width_type = conn.width_type;
        send_meta.driver_module = conn.driver.module;
        send_meta.driver_port = conn.driver.port;
        send_meta.array_size = conn.driver.port ? conn.driver.port->array_size : 0;

        gen.bus_plan.simWorkerPlans[pid].sendMBusCOutputs.push_back(send_rec);
        wp.send_to_top.push_back(send_meta);
      }
    }
  }

  // External inputs (Ei) : comb -> external (MBus)
  for (const auto& conn : analysis.external_inputs) {
    SignalEndpoint receiver = conn.receivers.empty() ? SignalEndpoint{} : conn.receivers.front();
    add_top_slot(conn.width, conn.width_type, conn.driver, receiver, true,
                 gen.top.recv_external_inputs, gen.bus_plan.topModulePlan.externalInput);
    if (conn.driver.module) {
      int pid = conn.driver.module->partition_id;
      auto& wp = ensure_worker_plan(pid, gen);
      register_module(conn.driver, wp, gen.top);
      register_module(receiver, wp, gen.top);
      int slices = slice_count_for_width(conn.width);
      for (int i = 0; i < slices; ++i) {
        const int slot_id = gen.top.next_slot - slices + i;
        SlotSendRecord send_rec;
        send_rec.portName = conn.driver.port ? conn.driver.port->name : conn.port_name;
        send_rec.bitOffset = i * 16;
        send_rec.targetId = 0;
        send_rec.slotId = slot_id;

        SlotSendMeta send_meta;
        send_meta.record = send_rec;
        send_meta.width = conn.width;
        send_meta.width_type = conn.width_type;
        send_meta.driver_module = conn.driver.module;
        send_meta.driver_port = conn.driver.port;
        send_meta.to_external = true;
        send_meta.array_size = conn.driver.port ? conn.driver.port->array_size : 0;

        gen.bus_plan.simWorkerPlans[pid].sendMBusCOutputs.push_back(send_rec);
        wp.send_to_top.push_back(send_meta);
      }
    }
  }

  // Local C->S (copy)
  for (const auto& kv : analysis.partitions) {
    int pid = kv.first;
    auto& wp = ensure_worker_plan(pid, gen);
    for (const auto& conn : kv.second.local_c_to_s) {
      if (conn.receivers.empty()) continue;
      const auto& recv = conn.receivers.front();
      register_module(conn.driver, wp, gen.top);
      register_module(recv, wp, gen.top);

      CopyRecord rec;
      rec.portName = conn.port_name;
      gen.bus_plan.simWorkerPlans[pid].copySInputs.push_back(rec);

      CopyMeta meta;
      meta.record = rec;
      meta.driver_module = conn.driver.module;
      meta.driver_port = conn.driver.port;
      meta.receiver_module = recv.module;
      meta.receiver_port = recv.port;
      meta.width = conn.width;
      meta.width_type = conn.width_type;
      meta.array_size = recv.port ? recv.port->array_size : 0;
      wp.copy_cts.push_back(meta);
    }
  }

  // Local S->C (copy)
  for (const auto& kv : analysis.partitions) {
    int pid = kv.first;
    auto& wp = ensure_worker_plan(pid, gen);
    for (const auto& conn : kv.second.local_s_to_c) {
      if (conn.receivers.empty()) continue;
      const auto& recv = conn.receivers.front();
      register_module(conn.driver, wp, gen.top);
      register_module(recv, wp, gen.top);

      CopyRecord rec;
      rec.portName = conn.port_name;
      gen.bus_plan.simWorkerPlans[pid].copyLocalCInputs.push_back(rec);

      CopyMeta meta;
      meta.record = rec;
      meta.driver_module = conn.driver.module;
      meta.driver_port = conn.driver.port;
      meta.receiver_module = recv.module;
      meta.receiver_port = recv.port;
      meta.width = conn.width;
      meta.width_type = conn.width_type;
      meta.array_size = recv.port ? recv.port->array_size : 0;
      wp.copy_stc.push_back(meta);
    }
  }

  // Sort plans/meta for stable output
  sort_bus_plan(gen.bus_plan);
  sort_meta(gen.top.send_inputs);
  sort_meta(gen.top.send_external_outputs);
  sort_meta(gen.top.recv_outputs);
  sort_meta(gen.top.recv_external_inputs);
  for (auto& kv : gen.workers) {
    sort_meta(kv.second.mbus_recvs);
    sort_meta(kv.second.sbus_recvs);
    sort_meta(kv.second.send_to_top);
    sort_meta(kv.second.send_remote);
    std::sort(kv.second.copy_cts.begin(), kv.second.copy_cts.end(),
              [](const CopyMeta& a, const CopyMeta& b) { return a.record.portName < b.record.portName; });
    std::sort(kv.second.copy_stc.begin(), kv.second.copy_stc.end(),
              [](const CopyMeta& a, const CopyMeta& b) { return a.record.portName < b.record.portName; });
  }

  return gen;
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
                                const GenerationPlan& plan) {
  const ModuleInfo* external_mod = plan.top.external;
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
  os << "inline constexpr size_t kCorvusGenMBusCount = " << static_cast<size_t>(plan.mbus_count) << ";\n";
  os << "inline constexpr size_t kCorvusGenSBusCount = " << static_cast<size_t>(plan.sbus_count) << ";\n";
  os << "#endif\n\n";

  const std::string top_class = top_class_name(output_base);
  os << "class " << top_class << " : public CorvusTopModule {\n";
  os << "public:\n";
  os << "  class TopPortsGen : public TopPorts {\n";
  os << "  public:\n";
  for (const auto& kv : plan.top.top_inputs) {
    os << "    " << cpp_type_from_signal(kv.second) << " " << kv.first << ";\n";
  }
  for (const auto& kv : plan.top.top_outputs) {
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
                             const GenerationPlan& plan) {
  const ModuleInfo* external_mod = plan.top.external;
  std::set<std::string> module_headers;
  if (external_mod) module_headers.insert(external_mod->header_path);
  std::ostringstream os;
  const std::string top_class = top_class_name(output_base);
  const std::string top_header_name = top_class + ".h";

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

  // sendIAndEOutput
  os << "void " << top_class << "::sendIAndEOutput() {\n";
  os << "  auto* ports = static_cast<" << top_class << "::TopPortsGen*>(topPorts);\n";
  os << "  (void)ports;\n";
  if (external_mod) {
    os << "  auto* eHandle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eModule);\n";
    os << "  auto* ext = eHandle ? eHandle->mp : nullptr;\n";
    os << "  (void)ext;\n";
  } else {
    os << "  (void)eModule;\n";
  }
  if (plan.top.send_inputs.empty() && plan.top.send_external_outputs.empty()) {
    os << "  // No inputs or external outputs to send\n";
  } else {
    os << "  size_t mbus_rr = 0;\n";
    os << "  uint64_t payload = 0;\n";
    os << "  uint64_t slice_data = 0;\n";
    const auto emit_send_block = [&](const SlotSendMeta& meta) {
      const auto& rec = meta.record;
      const std::string src_expr = meta.from_external
        ? (std::string("ext->") + (meta.driver_port ? meta.driver_port->name : rec.portName))
        : (std::string("ports->") + rec.portName);
      const std::string guard = meta.from_external ? "ext" : "ports";
      os << "  {\n";
      os << "    const uint32_t targetId = " << rec.targetId << ";\n";
      os << "    CorvusBusEndpoint* ep = mBusEndpoints[mbus_rr % mBusEndpoints.size()];\n";
      os << "    ++mbus_rr;\n";
      if (meta.width_type == PortWidthType::VL_W) {
        os << "    int bitOffset = " << rec.bitOffset << ";\n";
        os << "    int word = bitOffset / 32;\n";
        os << "    int wordBit = bitOffset % 32;\n";
        os << "    if (" << guard << ") {\n";
        os << "      slice_data = static_cast<uint64_t>(" << src_expr << "[word]);\n";
        os << "      slice_data >>= wordBit;\n";
        os << "      slice_data &= 0xFFFFULL;\n";
        os << "      payload = static_cast<uint64_t>(" << rec.slotId << ");\n";
        os << "      payload |= (slice_data << 32);\n";
        os << "      ep->send(targetId, payload);\n";
        os << "    }\n";
      } else {
        os << "    uint64_t src_val = static_cast<uint64_t>(" << src_expr << ");\n";
        os << "    slice_data = (src_val >> " << rec.bitOffset << ") & 0xFFFFULL;\n";
        os << "    payload = static_cast<uint64_t>(" << rec.slotId << ");\n";
        os << "    payload |= (slice_data << 32);\n";
        os << "    ep->send(targetId, payload);\n";
      }
      os << "  }\n";
    };
    for (const auto& meta : plan.top.send_inputs) {
      emit_send_block(meta);
    }
    for (const auto& meta : plan.top.send_external_outputs) {
      emit_send_block(meta);
    }
  }
  os << "}\n\n";

  // loadOAndEInput
  os << "void " << top_class << "::loadOAndEInput() {\n";
  os << "  auto* ports = static_cast<" << top_class << "::TopPortsGen*>(topPorts);\n";
  os << "  (void)ports;\n";
  if (external_mod) {
    os << "  auto* eHandle = static_cast<VerilatorModuleHandle<" << ext_class << ">*>(eModule);\n";
    os << "  auto* ext = eHandle ? eHandle->mp : nullptr;\n";
    os << "  (void)ext;\n";
  } else {
    os << "  (void)eModule;\n";
  }
  if (plan.top.recv_outputs.empty() && plan.top.recv_external_inputs.empty()) {
    os << "  // No outputs or external inputs to load\n";
  } else {
    os << "  const uint64_t kSlotMask = 0xFFFFFFFFULL;\n";
    os << "  for (size_t ep = 0; ep < mBusEndpoints.size(); ++ep) {\n";
    os << "    int cnt = mBusEndpoints[ep]->bufferCnt();\n";
    os << "    for (int i = 0; i < cnt; ++i) {\n";
    os << "      uint64_t payload = mBusEndpoints[ep]->recv();\n";
    os << "      uint32_t slotId = static_cast<uint32_t>(payload & kSlotMask);\n";
    os << "      switch (slotId) {\n";
    auto emit_recv_case = [&](const SlotRecvMeta& meta) {
      const auto& rec = meta.record;
      const std::string dst_expr = meta.to_external
        ? (std::string("ext->") + (meta.receiver_port ? meta.receiver_port->name : rec.portName))
        : (std::string("ports->") + rec.portName);
      const std::string guard = meta.to_external ? "ext" : "ports";
      os << "      case " << rec.slotId << ": {\n";
      os << "        uint64_t data = (payload >> 32) & 0xFFFFULL;\n";
      os << "        int bitOffset = " << rec.bitOffset << ";\n";
      if (meta.width_type == PortWidthType::VL_W) {
        os << "        int word = bitOffset / 32;\n";
        os << "        int wordBit = bitOffset % 32;\n";
        os << "        if (" << guard << ") {\n";
        os << "          uint32_t lowMask = static_cast<uint32_t>(0xFFFFULL << wordBit);\n";
        os << "          uint32_t lowBits = static_cast<uint32_t>(data << wordBit);\n";
        os << "          " << dst_expr << "[word] = (" << dst_expr << "[word] & ~lowMask) | lowBits;\n";
        os << "        }\n";
      } else if (meta.width <= 16) {
        os << "        if (" << guard << ") {\n";
        os << "          " << dst_expr << " = static_cast<" << cpp_type_from_meta(meta) << ">(data);\n";
        os << "        }\n";
      } else {
        os << "        uint64_t cur = static_cast<uint64_t>(" << dst_expr << ");\n";
        os << "        uint64_t mask = (0xFFFFULL) << bitOffset;\n";
        os << "        cur = (cur & ~mask) | ((data & 0xFFFFULL) << bitOffset);\n";
        os << "        " << dst_expr << " = static_cast<" << cpp_type_from_meta(meta) << ">(cur);\n";
      }
      os << "        break;\n";
      os << "      }\n";
    };
    for (const auto& meta : plan.top.recv_outputs) {
      emit_recv_case(meta);
    }
    for (const auto& meta : plan.top.recv_external_inputs) {
      emit_recv_case(meta);
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
                                   const WorkerGenPlan& wp,
                                   const GenerationPlan& plan,
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
  os << "inline constexpr size_t kCorvusGenMBusCount = " << static_cast<size_t>(plan.mbus_count) << ";\n";
  os << "inline constexpr size_t kCorvusGenSBusCount = " << static_cast<size_t>(plan.sbus_count) << ";\n";
  os << "#endif\n\n";

  std::string worker_class = worker_class_name(output_base, wp.pid);
  os << "class " << worker_class << " : public CorvusSimWorker {\n";
  os << "public:\n";
  os << "  " << worker_class << "(CorvusSimWorkerSynctreeEndpoint* simWorkerSynctreeEndpoint,\n";
  os << "                       std::vector<CorvusBusEndpoint*> mBusEndpoints,\n";
  os << "                       std::vector<CorvusBusEndpoint*> sBusEndpoints);\n";
  os << "protected:\n";
  os << "  void createSimModules() override;\n";
  os << "  void deleteSimModules() override;\n";
  os << "  void loadMBusCInputs() override;\n";
  os << "  void loadSBusCInputs() override;\n";
  os << "  void sendMBusCOutputs() override;\n";
  os << "  void copySInputs() override;\n";
  os << "  void sendSBusSOutputs() override;\n";
  os << "  void copyLocalCInputs() override;\n";
  os << "};\n\n";
  os << "} // namespace corvus_generated\n";
  os << "#endif // " << guard << "\n";
  return os.str();
}

std::string generate_worker_cpp(const std::string& output_base,
                                const WorkerGenPlan& wp,
                                const GenerationPlan& plan,
                                const std::set<std::string>& module_headers) {
  std::ostringstream os;
  (void)plan;
  const std::string worker_class = worker_class_name(output_base, wp.pid);
  os << "#include \"" << path_basename(worker_class + ".h") << "\"\n";
  for (const auto& h : module_headers) {
    os << "#include \"" << h << "\"\n";
  }
  os << "\nnamespace corvus_generated {\n\n";

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

  // loadMBusCInputs
  os << "void " << worker_class << "::loadMBusCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  os << "  const uint64_t kSlotMask = 0xFFFFFFFFULL;\n";
  if (wp.mbus_recvs.empty()) {
    os << "  (void)kSlotMask;\n";
  } else {
    os << "  for (size_t ep = 0; ep < mBusEndpoints.size(); ++ep) {\n";
    os << "    int cnt = mBusEndpoints[ep]->bufferCnt();\n";
    os << "    for (int i = 0; i < cnt; ++i) {\n";
    os << "      uint64_t payload = mBusEndpoints[ep]->recv();\n";
    os << "      uint32_t slotId = static_cast<uint32_t>(payload & kSlotMask);\n";
    os << "      switch (slotId) {\n";
    for (const auto& meta : wp.mbus_recvs) {
      const auto& rec = meta.record;
      const std::string dst_expr = std::string("comb->") + (meta.receiver_port ? meta.receiver_port->name : rec.portName);
      os << "      case " << rec.slotId << ": {\n";
      os << "        uint64_t data = (payload >> 32) & 0xFFFFULL;\n";
      os << "        int bitOffset = " << rec.bitOffset << ";\n";
      if (meta.width_type == PortWidthType::VL_W) {
        os << "        int word = bitOffset / 32;\n";
        os << "        int wordBit = bitOffset % 32;\n";
        os << "        uint32_t lowMask = static_cast<uint32_t>(0xFFFFULL << wordBit);\n";
        os << "        uint32_t lowBits = static_cast<uint32_t>(data << wordBit);\n";
        os << "        " << dst_expr << "[word] = (" << dst_expr << "[word] & ~lowMask) | lowBits;\n";
      } else if (meta.width <= 16) {
        os << "        " << dst_expr << " = static_cast<" << cpp_type_from_meta(meta) << ">(data);\n";
      } else {
        os << "        uint64_t cur = static_cast<uint64_t>(" << dst_expr << ");\n";
        os << "        uint64_t mask = (0xFFFFULL) << bitOffset;\n";
        os << "        cur = (cur & ~mask) | ((data & 0xFFFFULL) << bitOffset);\n";
        os << "        " << dst_expr << " = static_cast<" << cpp_type_from_meta(meta) << ">(cur);\n";
      }
      os << "        break;\n";
      os << "      }\n";
    }
    os << "      default: break;\n";
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
  }
  os << "}\n\n";

  // loadSBusCInputs
  os << "void " << worker_class << "::loadSBusCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  os << "  const uint64_t kSlotMask = 0xFFFFFFFFULL;\n";
  if (wp.sbus_recvs.empty()) {
    os << "  (void)kSlotMask;\n";
  } else {
    os << "  for (size_t ep = 0; ep < sBusEndpoints.size(); ++ep) {\n";
    os << "    int cnt = sBusEndpoints[ep]->bufferCnt();\n";
    os << "    for (int i = 0; i < cnt; ++i) {\n";
    os << "      uint64_t payload = sBusEndpoints[ep]->recv();\n";
    os << "      uint32_t slotId = static_cast<uint32_t>(payload & kSlotMask);\n";
    os << "      switch (slotId) {\n";
    for (const auto& meta : wp.sbus_recvs) {
      const auto& rec = meta.record;
      const std::string dst_expr = std::string("comb->") + (meta.receiver_port ? meta.receiver_port->name : rec.portName);
      os << "      case " << rec.slotId << ": {\n";
      os << "        uint64_t data = (payload >> 32) & 0xFFFFULL;\n";
      os << "        int bitOffset = " << rec.bitOffset << ";\n";
      if (meta.width_type == PortWidthType::VL_W) {
        os << "        int word = bitOffset / 32;\n";
        os << "        int wordBit = bitOffset % 32;\n";
        os << "        uint32_t lowMask = static_cast<uint32_t>(0xFFFFULL << wordBit);\n";
        os << "        uint32_t lowBits = static_cast<uint32_t>(data << wordBit);\n";
        os << "        " << dst_expr << "[word] = (" << dst_expr << "[word] & ~lowMask) | lowBits;\n";
      } else if (meta.width <= 16) {
        os << "        " << dst_expr << " = static_cast<" << cpp_type_from_meta(meta) << ">(data);\n";
      } else {
        os << "        uint64_t cur = static_cast<uint64_t>(" << dst_expr << ");\n";
        os << "        uint64_t mask = (0xFFFFULL) << bitOffset;\n";
        os << "        cur = (cur & ~mask) | ((data & 0xFFFFULL) << bitOffset);\n";
        os << "        " << dst_expr << " = static_cast<" << cpp_type_from_meta(meta) << ">(cur);\n";
      }
      os << "        break;\n";
      os << "      }\n";
    }
    os << "      default: break;\n";
    os << "      }\n";
    os << "    }\n";
    os << "  }\n";
  }
  os << "}\n\n";

  // sendMBusCOutputs
  os << "void " << worker_class << "::sendMBusCOutputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  if (!comb) return;\n";
  if (wp.send_to_top.empty()) {
    os << "  (void)comb;\n";
  } else {
    os << "  size_t mbus_rr = 0;\n";
    os << "  uint64_t payload = 0;\n";
    os << "  uint64_t slice_data = 0;\n";
    for (const auto& meta : wp.send_to_top) {
      const auto& rec = meta.record;
      std::string src = std::string("comb->") + (meta.driver_port ? meta.driver_port->name : rec.portName);
      os << "  {\n";
      os << "    // slot " << rec.slotId << "\n";
      os << "    CorvusBusEndpoint* ep = mBusEndpoints[mbus_rr % mBusEndpoints.size()];\n";
      os << "    ++mbus_rr;\n";
      if (meta.width_type == PortWidthType::VL_W) {
        os << "    int bitOffset = " << rec.bitOffset << ";\n";
        os << "    int word = bitOffset / 32;\n";
        os << "    int wordBit = bitOffset % 32;\n";
        os << "    slice_data = static_cast<uint64_t>(" << src << "[word]);\n";
        os << "    slice_data >>= wordBit;\n";
        os << "    slice_data &= 0xFFFFULL;\n";
        os << "    payload = static_cast<uint64_t>(" << rec.slotId << ");\n";
        os << "    payload |= (slice_data << 32);\n";
        os << "    ep->send(0, payload);\n";
      } else {
        os << "    uint64_t src_val = static_cast<uint64_t>(" << src << ");\n";
        os << "    slice_data = (src_val >> " << rec.bitOffset << ") & 0xFFFFULL;\n";
        os << "    payload = static_cast<uint64_t>(" << rec.slotId << ");\n";
        os << "    payload |= (slice_data << 32);\n";
        os << "    ep->send(0, payload);\n";
      }
      os << "  }\n";
    }
  }
  os << "}\n\n";

  // copySInputs (C->S local copy)
  os << "void " << worker_class << "::copySInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!comb || !seq) return;\n";
  for (const auto& meta : wp.copy_cts) {
    if (!meta.driver_port || !meta.receiver_port) continue;
    std::string src = "comb->" + meta.driver_port->name;
    std::string dst = "seq->" + meta.receiver_port->name;
    if (meta.width_type == PortWidthType::VL_W) {
      int words = meta.receiver_port->array_size;
      os << "  for (int i = 0; i < " << words << "; ++i) { " << dst << "[i] = " << src << "[i]; }\n";
    } else {
      os << "  " << dst << " = " << src << ";\n";
    }
  }
  os << "}\n\n";

  // sendSBusSOutputs
  os << "void " << worker_class << "::sendSBusSOutputs() {\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!seq) return;\n";
  if (wp.send_remote.empty()) {
    os << "  (void)seq;\n";
  } else {
    os << "  size_t sbus_rr = 0;\n";
    os << "  uint64_t payload = 0;\n";
    os << "  uint64_t slice_data = 0;\n";
    for (const auto& meta : wp.send_remote) {
      const auto& rec = meta.record;
      std::string src = "seq->" + (meta.driver_port ? meta.driver_port->name : rec.portName);
      os << "  {\n";
      os << "    const uint32_t targetId = " << rec.targetId << ";\n";
      os << "    CorvusBusEndpoint* ep = sBusEndpoints[sbus_rr % sBusEndpoints.size()];\n";
      os << "    ++sbus_rr;\n";
      if (meta.width_type == PortWidthType::VL_W) {
        os << "    int bitOffset = " << rec.bitOffset << ";\n";
        os << "    int word = bitOffset / 32;\n";
        os << "    int wordBit = bitOffset % 32;\n";
        os << "    slice_data = static_cast<uint64_t>(" << src << "[word]);\n";
        os << "    slice_data >>= wordBit;\n";
        os << "    slice_data &= 0xFFFFULL;\n";
        os << "    payload = static_cast<uint64_t>(" << rec.slotId << ");\n";
        os << "    payload |= (slice_data << 32);\n";
        os << "    ep->send(targetId, payload);\n";
      } else {
        os << "    uint64_t src_val = static_cast<uint64_t>(" << src << ");\n";
        os << "    slice_data = (src_val >> " << rec.bitOffset << ") & 0xFFFFULL;\n";
        os << "    payload = static_cast<uint64_t>(" << rec.slotId << ");\n";
        os << "    payload |= (slice_data << 32);\n";
        os << "    ep->send(targetId, payload);\n";
      }
      os << "  }\n";
    }
  }
  os << "}\n\n";

  // copyLocalCInputs (S->C local copy)
  os << "void " << worker_class << "::copyLocalCInputs() {\n";
  os << "  auto* combHandle = static_cast<VerilatorModuleHandle<" << wp.comb->class_name << ">* >(cModule);\n";
  os << "  auto* seqHandle = static_cast<VerilatorModuleHandle<" << wp.seq->class_name << ">* >(sModule);\n";
  os << "  auto* comb = combHandle ? combHandle->mp : nullptr;\n";
  os << "  auto* seq = seqHandle ? seqHandle->mp : nullptr;\n";
  os << "  if (!comb || !seq) return;\n";
  for (const auto& meta : wp.copy_stc) {
    if (!meta.driver_port || !meta.receiver_port) continue;
    std::string src = "seq->" + meta.driver_port->name;
    std::string dst = "comb->" + meta.receiver_port->name;
    if (meta.width_type == PortWidthType::VL_W) {
      int words = meta.receiver_port->array_size;
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

bool CorvusGenerator::write_connection_analysis_json(const ConnectionAnalysis& analysis,
                                                     const std::string& output_base) const {
  const std::string json_path = output_base + "_connection_analysis.json";
  std::ofstream ofs(json_path);
  if (!ofs.is_open()) {
    std::cerr << "Failed to open output: " << json_path << std::endl;
    return false;
  }

  auto write_endpoint = [&](const SignalEndpoint& ep) {
    ofs << "{ \"module\": \"" << (ep.module ? ep.module->instance_name : "null")
        << "\", \"module_type\": \"" << (ep.module ? ep.module->get_type_str() : "null")
        << "\", \"partition\": " << (ep.module ? ep.module->partition_id : -1)
        << ", \"port\": \"" << (ep.port ? ep.port->name : "null") << "\" }";
  };

  auto write_connections = [&](const std::vector<ClassifiedConnection>& conns) {
    ofs << "[";
    for (size_t i = 0; i < conns.size(); ++i) {
      const auto& c = conns[i];
      ofs << "{ \"port\": \"" << c.port_name << "\", \"width\": " << c.width
          << ", \"width_type\": \"" << width_type_to_string(c.width_type) << "\", \"driver\": ";
      write_endpoint(c.driver);
      ofs << ", \"receivers\": [";
      for (size_t j = 0; j < c.receivers.size(); ++j) {
        write_endpoint(c.receivers[j]);
        if (j + 1 < c.receivers.size()) ofs << ", ";
      }
      ofs << "] }";
      if (i + 1 < conns.size()) ofs << ", ";
    }
    ofs << "]";
  };

  ofs << "{\n";
  ofs << "  \"warnings\": [";
  for (size_t i = 0; i < analysis.warnings.size(); ++i) {
    ofs << "\"" << analysis.warnings[i] << "\"";
    if (i + 1 < analysis.warnings.size()) ofs << ", ";
  }
  ofs << "],\n";

  ofs << "  \"top_inputs\": ";
  write_connections(analysis.top_inputs);
  ofs << ",\n  \"top_outputs\": ";
  write_connections(analysis.top_outputs);
  ofs << ",\n  \"external_inputs\": ";
  write_connections(analysis.external_inputs);
  ofs << ",\n  \"external_outputs\": ";
  write_connections(analysis.external_outputs);
  ofs << ",\n  \"partitions\": {\n";
  for (auto it = analysis.partitions.begin(); it != analysis.partitions.end(); ++it) {
    ofs << "    \"" << it->first << "\": {";
    ofs << "\"local_c_to_s\": ";
    write_connections(it->second.local_c_to_s);
    ofs << ", \"local_s_to_c\": ";
    write_connections(it->second.local_s_to_c);
    ofs << ", \"remote_s_to_c\": ";
    write_connections(it->second.remote_s_to_c);
    ofs << "}";
    auto next_it = it;
    ++next_it;
    if (next_it != analysis.partitions.end()) ofs << ",";
    ofs << "\n";
  }
  ofs << "  }\n";
  ofs << "}\n";
  ofs.close();
  std::cout << "Connection analysis wrote: " << json_path << std::endl;
  return true;
}

bool CorvusGenerator::write_bus_plan_json(const CorvusBusPlan& plan,
                                          const std::vector<std::string>& warnings,
                                          const std::string& output_base) const {
  const std::string json_path = output_base + "_corvus_bus_plan.json";
  std::ofstream ofs(json_path);
  if (!ofs.is_open()) {
    std::cerr << "Failed to open output: " << json_path << std::endl;
    return false;
  }

  auto write_recv_vec = [&](const std::vector<SlotRecvRecord>& v) {
    ofs << "[";
    for (size_t i = 0; i < v.size(); ++i) {
      ofs << "{ \"portName\": \"" << v[i].portName << "\", \"slotId\": " << v[i].slotId
          << ", \"bitOffset\": " << v[i].bitOffset << "}";
      if (i + 1 < v.size()) ofs << ", ";
    }
    ofs << "]";
  };
  auto write_send_vec = [&](const std::vector<SlotSendRecord>& v) {
    ofs << "[";
    for (size_t i = 0; i < v.size(); ++i) {
      ofs << "{ \"portName\": \"" << v[i].portName << "\", \"bitOffset\": " << v[i].bitOffset
          << ", \"targetId\": " << v[i].targetId << ", \"slotId\": " << v[i].slotId << "}";
      if (i + 1 < v.size()) ofs << ", ";
    }
    ofs << "]";
  };
  auto write_copy_vec = [&](const std::vector<CopyRecord>& v) {
    ofs << "[";
    for (size_t i = 0; i < v.size(); ++i) {
      ofs << "{ \"portName\": \"" << v[i].portName << "\"}";
      if (i + 1 < v.size()) ofs << ", ";
    }
    ofs << "]";
  };

  ofs << "{\n";
  ofs << "  \"warnings\": [";
  for (size_t i = 0; i < warnings.size(); ++i) {
    ofs << "\"" << warnings[i] << "\"";
    if (i + 1 < warnings.size()) ofs << ", ";
  }
  ofs << "],\n";

  ofs << "  \"topModulePlan\": {\n";
  ofs << "    \"input\": ";
  write_send_vec(plan.topModulePlan.input);
  ofs << ",\n    \"output\": ";
  write_recv_vec(plan.topModulePlan.output);
  ofs << ",\n    \"externalInput\": ";
  write_recv_vec(plan.topModulePlan.externalInput);
  ofs << ",\n    \"externalOutput\": ";
  write_send_vec(plan.topModulePlan.externalOutput);
  ofs << "\n  },\n";

  ofs << "  \"simWorkerPlans\": {\n";
  for (auto it = plan.simWorkerPlans.begin(); it != plan.simWorkerPlans.end(); ++it) {
    ofs << "    \"" << it->first << "\": {\n";
    ofs << "      \"loadMBusCInputs\": ";
    write_recv_vec(it->second.loadMBusCInputs);
    ofs << ",\n      \"loadSBusCInputs\": ";
    write_recv_vec(it->second.loadSBusCInputs);
    ofs << ",\n      \"sendMBusCOutputs\": ";
    write_send_vec(it->second.sendMBusCOutputs);
    ofs << ",\n      \"copySInputs\": ";
    write_copy_vec(it->second.copySInputs);
    ofs << ",\n      \"sendSBusSOutputs\": ";
    write_send_vec(it->second.sendSBusSOutputs);
    ofs << ",\n      \"copyLocalCInputs\": ";
    write_copy_vec(it->second.copyLocalCInputs);
    ofs << "\n    }";
    auto next_it = it;
    ++next_it;
    if (next_it != plan.simWorkerPlans.end()) ofs << ",";
    ofs << "\n";
  }
  ofs << "  }\n";
  ofs << "}\n";
  ofs.close();
  std::cout << "Corvus bus plan wrote: " << json_path << std::endl;
  return true;
}

bool CorvusGenerator::generate(const ConnectionAnalysis& analysis,
                               const std::string& output_base,
                               int mbus_count,
                               int sbus_count) {
  std::string stage = "init";
  try {
    stage = "build_generation_plan";
    GenerationPlan plan = build_generation_plan(analysis, mbus_count, sbus_count);
    stage = "write_connection_analysis";
    if (!write_connection_analysis_json(analysis, output_base)) {
      return false;
    }
    stage = "write_bus_plan";
    if (!write_bus_plan_json(plan.bus_plan, plan.warnings, output_base)) {
      return false;
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
        wc << generate_worker_cpp(output_base, wp, plan, worker_headers_set);
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
