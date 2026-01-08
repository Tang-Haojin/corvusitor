#include "../include/connection_builder.h"
#include <iostream>
#include <string>
#include <vector>

namespace {

PortInfo make_port(const std::string& name, PortDirection dir, PortWidthType type, int msb = 0, int lsb = 0) {
  PortInfo p;
  p.name = name;
  p.direction = dir;
  p.width_type = type;
  p.msb = msb;
  p.lsb = lsb;
  p.array_size = 1;
  return p;
}

ModuleInfo make_module(const std::string& module_name,
                       ModuleType type,
                       int partition_id,
                       const std::vector<PortInfo>& ports) {
  ModuleInfo m;
  m.module_name = module_name;
  m.class_name = "V" + module_name;
  m.instance_name = module_name;
  m.type = type;
  m.partition_id = partition_id;
  m.ports = ports;
  return m;
}

bool expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAILED: " << msg << std::endl;
  }
  return cond;
}

const ClassifiedConnection* find_by_name(const std::vector<ClassifiedConnection>& conns,
                                          const std::string& name) {
  for (const auto& c : conns) {
    if (c.port_name == name) return &c;
  }
  return nullptr;
}

} // namespace

int main() {
  // Build a minimal graph covering top IO, external IO, local and remote links.
  ModuleInfo comb0 = make_module("corvus_comb_P0", ModuleType::COMB, 0, {
    make_port("top_in", PortDirection::INPUT, PortWidthType::VL_32, 31, 0),
    make_port("to_seq", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0),
    make_port("to_ext", PortDirection::OUTPUT, PortWidthType::VL_16, 15, 0),
    make_port("from_seq", PortDirection::INPUT, PortWidthType::VL_8, 0, 0),
    make_port("top_out0", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0)
  });

  ModuleInfo seq0 = make_module("corvus_seq_P0", ModuleType::SEQ, 0, {
    make_port("to_seq", PortDirection::INPUT, PortWidthType::VL_8, 0, 0),
    make_port("from_seq", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0),
    make_port("remote_sig", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0)
  });

  ModuleInfo comb1 = make_module("corvus_comb_P1", ModuleType::COMB, 1, {
    make_port("remote_sig", PortDirection::INPUT, PortWidthType::VL_8, 0, 0),
    make_port("from_ext", PortDirection::INPUT, PortWidthType::VL_16, 15, 0),
    make_port("top_out1", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0)
  });

  ModuleInfo external = make_module("corvus_external", ModuleType::EXTERNAL, -1, {
    make_port("to_ext", PortDirection::INPUT, PortWidthType::VL_16, 15, 0),
    make_port("from_ext", PortDirection::OUTPUT, PortWidthType::VL_16, 15, 0)
  });

  std::vector<ModuleInfo> modules = {comb0, seq0, comb1, external};

  ConnectionBuilder builder;
  ConnectionAnalysis analysis = builder.analyze(modules);

  int failures = 0;

  failures += !expect(analysis.warnings.empty(), "No warnings expected");

  // Top input
  const auto* top_in = find_by_name(analysis.top_inputs, "top_in");
  failures += !expect(top_in && top_in->receivers.size() == 1, "top_in classified");

  // Top outputs
  failures += !expect(find_by_name(analysis.top_outputs, "top_out0"), "top_out0 classified");
  failures += !expect(find_by_name(analysis.top_outputs, "top_out1"), "top_out1 classified");

  // External IO
  failures += !expect(find_by_name(analysis.external_inputs, "to_ext"), "Ei classified");
  failures += !expect(find_by_name(analysis.external_outputs, "from_ext"), "Eo classified");

  // Partition 0 locals
  const auto& p0 = analysis.partitions[0];
  failures += !expect(find_by_name(p0.local_cts_to_si, "to_seq"), "localCtSi in P0");
  failures += !expect(find_by_name(p0.local_stc_to_ci, "from_seq"), "localStCi in P0");

  // Partition 0 remote
  const auto* remote = find_by_name(p0.remote_s_to_c, "remote_sig");
  failures += !expect(remote && remote->receivers.size() == 1 &&
                      remote->receivers[0].module &&
                      remote->receivers[0].module->partition_id == 1,
                      "remoteSitCj captured with receiver in P1");

  if (failures == 0) {
    std::cout << "connection_analysis: PASS" << std::endl;
    return 0;
  }

  std::cerr << "connection_analysis: FAIL (" << failures << " checks)\n";
  return 1;
}
