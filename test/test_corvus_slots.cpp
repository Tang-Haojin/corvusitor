#include "../include/corvus_generator.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {
PortInfo make_port(const std::string& name, PortDirection dir, PortWidthType type,
                   int msb, int lsb) {
  PortInfo p;
  p.name = name;
  p.direction = dir;
  p.width_type = type;
  p.msb = msb;
  p.lsb = lsb;
  p.array_size = 0;
  return p;
}

SignalEndpoint make_endpoint(const ModuleInfo& mod, const PortInfo& port) {
  SignalEndpoint ep;
  ep.module = &mod;
  ep.port = &port;
  return ep;
}
} // namespace

// Validate multi-partition slot assignment and remote S->C emission.
int main() {
  std::system("mkdir -p build");

  ModuleInfo comb0;
  comb0.module_name = "corvus_comb_P0";
  comb0.class_name = "Vcorvus_comb_P0";
  comb0.instance_name = "comb_p0";
  comb0.type = ModuleType::COMB;
  comb0.partition_id = 0;
  comb0.header_path = "Vcorvus_comb_P0.h";
  comb0.ports.push_back(make_port("in_top", PortDirection::INPUT, PortWidthType::VL_8, 0, 0));
  comb0.ports.push_back(make_port("c0_to_s0", PortDirection::OUTPUT, PortWidthType::VL_8, 7, 0));

  ModuleInfo seq0;
  seq0.module_name = "corvus_seq_P0";
  seq0.class_name = "Vcorvus_seq_P0";
  seq0.instance_name = "seq_p0";
  seq0.type = ModuleType::SEQ;
  seq0.partition_id = 0;
  seq0.header_path = "Vcorvus_seq_P0.h";
  seq0.ports.push_back(make_port("c0_to_s0", PortDirection::INPUT, PortWidthType::VL_8, 7, 0));
  seq0.ports.push_back(make_port("s0_to_c1", PortDirection::OUTPUT, PortWidthType::VL_16, 15, 0));

  ModuleInfo comb1;
  comb1.module_name = "corvus_comb_P1";
  comb1.class_name = "Vcorvus_comb_P1";
  comb1.instance_name = "comb_p1";
  comb1.type = ModuleType::COMB;
  comb1.partition_id = 1;
  comb1.header_path = "Vcorvus_comb_P1.h";
  comb1.ports.push_back(make_port("s0_to_c1", PortDirection::INPUT, PortWidthType::VL_16, 15, 0));
  comb1.ports.push_back(make_port("out_top", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0));

  ModuleInfo seq1;
  seq1.module_name = "corvus_seq_P1";
  seq1.class_name = "Vcorvus_seq_P1";
  seq1.instance_name = "seq_p1";
  seq1.type = ModuleType::SEQ;
  seq1.partition_id = 1;
  seq1.header_path = "Vcorvus_seq_P1.h";
  seq1.ports.push_back(make_port("stub_in", PortDirection::INPUT, PortWidthType::VL_8, 0, 0));
  seq1.ports.push_back(make_port("stub_out", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0));

  ConnectionAnalysis analysis;

  // Top input -> comb0
  ClassifiedConnection in_top;
  in_top.port_name = "in_top";
  in_top.width = 8;
  in_top.width_type = PortWidthType::VL_8;
  in_top.receivers.push_back(make_endpoint(comb0, comb0.ports[0]));
  analysis.top_inputs.push_back(in_top);

  // Top output from comb1
  ClassifiedConnection out_top;
  out_top.port_name = "out_top";
  out_top.width = 8;
  out_top.width_type = PortWidthType::VL_8;
  out_top.driver = make_endpoint(comb1, comb1.ports[1]);
  analysis.top_outputs.push_back(out_top);

  // Local C->S in P0
  ClassifiedConnection cts;
  cts.port_name = "c0_to_s0";
  cts.width = 8;
  cts.width_type = PortWidthType::VL_8;
  cts.driver = make_endpoint(comb0, comb0.ports[1]);
  cts.receivers.push_back(make_endpoint(seq0, seq0.ports[0]));
  analysis.partitions[0].local_c_to_s.push_back(cts);

  // Remote S->C from seq0 to comb1
  ClassifiedConnection remote;
  remote.port_name = "s0_to_c1";
  remote.width = 16;
  remote.width_type = PortWidthType::VL_16;
  remote.driver = make_endpoint(seq0, seq0.ports[1]);
  remote.receivers.push_back(make_endpoint(comb1, comb1.ports[0]));
  analysis.partitions[0].remote_s_to_c.push_back(remote);

  // Dummy local loop in P1
  ClassifiedConnection local_p1;
  local_p1.port_name = "stub";
  local_p1.width = 8;
  local_p1.width_type = PortWidthType::VL_8;
  local_p1.driver = make_endpoint(seq1, seq1.ports[1]);
  local_p1.receivers.push_back(make_endpoint(comb1, comb1.ports[0]));
  analysis.partitions[1].local_s_to_c.push_back(local_p1);

  CorvusGenerator gen;
  const std::string base = "build/corvus_slot_test";
  if (!gen.generate(analysis, base, 1, 1)) {
    std::cerr << "CorvusGenerator failed\n";
    return 1;
  }

  // Verify JSON includes remote_s_to_c
  std::ifstream ifs(base + "_corvus.json");
  if (!ifs.is_open()) {
    std::cerr << "Missing output json\n";
    return 1;
  }
  std::string json_content((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
  if (json_content.find("remote_s_to_c") == std::string::npos) {
    std::cerr << "remote_s_to_c not found in JSON\n";
    return 1;
  }

  // Verify header has both worker classes and remote target id for pid1 (targetId=2).
  std::ifstream hfs(base + "_corvus_gen.h");
  if (!hfs.is_open()) {
    std::cerr << "Missing generated header\n";
    return 1;
  }
  std::string header_content((std::istreambuf_iterator<char>(hfs)),
                             std::istreambuf_iterator<char>());
  if (header_content.find("CorvusSimWorkerGenP0") == std::string::npos ||
      header_content.find("CorvusSimWorkerGenP1") == std::string::npos) {
    std::cerr << "Worker class definitions missing\n";
    return 1;
  }
  if (header_content.find("targetId = 2") == std::string::npos) {
    std::cerr << "Remote target ID not emitted as expected\n";
    return 1;
  }

  std::cout << "corvus_slots: PASS\n";
  return 0;
}
