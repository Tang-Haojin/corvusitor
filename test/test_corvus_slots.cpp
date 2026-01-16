#include "../include/corvus_generator.h"
#include <cctype>
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

std::string class_prefix(const std::string& output_base) {
  std::string token = output_base;
  size_t pos = token.find_last_of("/\\");
  if (pos != std::string::npos) token = token.substr(pos + 1);
  for (char& c : token) {
    if (!std::isalnum(static_cast<unsigned char>(c))) {
      c = '_';
    }
  }
  if (token.empty()) token = "output";
  if (std::isdigit(static_cast<unsigned char>(token.front()))) {
    token.insert(token.begin(), '_');
  }
  return "C" + token;
}

std::string path_dirname(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  if (pos == 0) return path.substr(0, 1);
  return path.substr(0, pos);
}

std::string join_path(const std::string& dir, const std::string& file) {
  if (dir.empty() || dir == ".") return file;
  if (dir.back() == '/' || dir.back() == '\\') return dir + file;
  return dir + "/" + file;
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
  std::ifstream plan(base + "_corvus_bus_plan.json");
  if (!plan.is_open()) {
    std::cerr << "Missing output json\n";
    return 1;
  }
  std::string json_content((std::istreambuf_iterator<char>(plan)),
                           std::istreambuf_iterator<char>());
  if (json_content.find("sendSBusSOutputs") == std::string::npos ||
      json_content.find("s0_to_c1") == std::string::npos) {
    std::cerr << "s0_to_c1 mapping not found in JSON\n";
    return 1;
  }

  // Verify header/cpp has both worker classes and remote target id for pid1 (targetId=2).
  const std::string out_dir = path_dirname(base);
  const std::string prefix = class_prefix(base);
  std::ifstream agg(join_path(out_dir, prefix + "CorvusGen.h"));
  std::ifstream worker0_h(join_path(out_dir, prefix + "SimWorkerGenP0.h"));
  std::ifstream worker1_h(join_path(out_dir, prefix + "SimWorkerGenP1.h"));
  std::ifstream worker0_cpp(join_path(out_dir, prefix + "SimWorkerGenP0.cpp"));
  if (!agg.is_open() || !worker0_h.is_open() || !worker1_h.is_open() || !worker0_cpp.is_open()) {
    std::cerr << "Missing generated top/worker artifacts\n";
    return 1;
  }
  std::string worker0_header((std::istreambuf_iterator<char>(worker0_h)),
                             std::istreambuf_iterator<char>());
  std::string worker1_header((std::istreambuf_iterator<char>(worker1_h)),
                             std::istreambuf_iterator<char>());
  std::string worker0_cpp_content((std::istreambuf_iterator<char>(worker0_cpp)),
                                  std::istreambuf_iterator<char>());
  if (worker0_header.find(prefix + "SimWorkerGenP0") == std::string::npos ||
      worker1_header.find(prefix + "SimWorkerGenP1") == std::string::npos) {
    std::cerr << "Worker class definitions missing\n";
    return 1;
  }
  if (worker0_cpp_content.find("targetId = 2") == std::string::npos) {
    std::cerr << "Remote target ID not emitted as expected\n";
    return 1;
  }

  std::cout << "corvus_slots: PASS\n";
  return 0;
}
