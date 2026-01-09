#include "../include/corvus_generator.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {
PortInfo make_port(const std::string& name, PortDirection dir, PortWidthType type,
                   int msb, int lsb, int array_size = 0) {
  PortInfo p;
  p.name = name;
  p.direction = dir;
  p.width_type = type;
  p.msb = msb;
  p.lsb = lsb;
  p.array_size = array_size;
  return p;
}
} // namespace

SignalEndpoint make_endpoint(const ModuleInfo& mod, const PortInfo& port) {
  SignalEndpoint ep;
  ep.module = &mod;
  ep.port = &port;
  return ep;
}

// Smoke test: emit json + generated header from a synthetic corvus topology.
int main() {
  std::system("mkdir -p build");

  ModuleInfo comb;
  comb.module_name = "corvus_comb_P0";
  comb.class_name = "Vcorvus_comb_P0";
  comb.instance_name = "comb_p0";
  comb.type = ModuleType::COMB;
  comb.partition_id = 0;
  comb.header_path = "Vcorvus_comb_P0.h";
  comb.ports.push_back(make_port("in0", PortDirection::INPUT, PortWidthType::VL_8, 0, 0));
  comb.ports.push_back(make_port("eo0_in", PortDirection::INPUT, PortWidthType::VL_8, 0, 0));
  comb.ports.push_back(make_port("ei0_out", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0));
  comb.ports.push_back(make_port("out0", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0));
  comb.ports.push_back(make_port("sig", PortDirection::OUTPUT, PortWidthType::VL_8, 7, 0));
  comb.ports.push_back(make_port("sig_back", PortDirection::INPUT, PortWidthType::VL_8, 7, 0));

  ModuleInfo seq;
  seq.module_name = "corvus_seq_P0";
  seq.class_name = "Vcorvus_seq_P0";
  seq.instance_name = "seq_p0";
  seq.type = ModuleType::SEQ;
  seq.partition_id = 0;
  seq.header_path = "Vcorvus_seq_P0.h";
  seq.ports.push_back(make_port("sig", PortDirection::INPUT, PortWidthType::VL_8, 7, 0));
  seq.ports.push_back(make_port("sig_back", PortDirection::OUTPUT, PortWidthType::VL_8, 7, 0));

  ModuleInfo ext;
  ext.module_name = "corvus_external";
  ext.class_name = "Vcorvus_external";
  ext.instance_name = "external";
  ext.type = ModuleType::EXTERNAL;
  ext.partition_id = -1;
  ext.header_path = "Vcorvus_external.h";
  ext.ports.push_back(make_port("ei0", PortDirection::INPUT, PortWidthType::VL_8, 0, 0));
  ext.ports.push_back(make_port("eo0", PortDirection::OUTPUT, PortWidthType::VL_8, 0, 0));

  ConnectionAnalysis analysis;

  // Top input -> comb in0
  ClassifiedConnection top_in;
  top_in.port_name = "in0";
  top_in.width = 1;
  top_in.width_type = PortWidthType::VL_8;
  top_in.receivers.push_back(make_endpoint(comb, comb.ports[0]));
  analysis.top_inputs.push_back(top_in);

  // Top output out0 from comb
  ClassifiedConnection top_out;
  top_out.port_name = "out0";
  top_out.width = 1;
  top_out.width_type = PortWidthType::VL_8;
  top_out.driver = make_endpoint(comb, comb.ports[3]);
  analysis.top_outputs.push_back(top_out);

  // External input ei0 (comb -> external)
  ClassifiedConnection ei;
  ei.port_name = "ei0";
  ei.width = 1;
  ei.width_type = PortWidthType::VL_8;
  ei.driver = make_endpoint(comb, comb.ports[2]);
  ei.receivers.push_back(make_endpoint(ext, ext.ports[0]));
  analysis.external_inputs.push_back(ei);

  // External output eo0 (external -> comb)
  ClassifiedConnection eo;
  eo.port_name = "eo0";
  eo.width = 1;
  eo.width_type = PortWidthType::VL_8;
  eo.driver = make_endpoint(ext, ext.ports[1]);
  eo.receivers.push_back(make_endpoint(comb, comb.ports[1]));
  analysis.external_outputs.push_back(eo);

  // Local C->S (sig)
  ClassifiedConnection local_cts;
  local_cts.port_name = "sig";
  local_cts.width = 8;
  local_cts.width_type = PortWidthType::VL_8;
  local_cts.driver = make_endpoint(comb, comb.ports[4]);
  local_cts.receivers.push_back(make_endpoint(seq, seq.ports[0]));
  analysis.partitions[0].local_cts_to_si.push_back(local_cts);

  // Local S->C (sig_back)
  ClassifiedConnection local_stc;
  local_stc.port_name = "sig_back";
  local_stc.width = 8;
  local_stc.width_type = PortWidthType::VL_8;
  local_stc.driver = make_endpoint(seq, seq.ports[1]);
  local_stc.receivers.push_back(make_endpoint(comb, comb.ports[5]));
  analysis.partitions[0].local_stc_to_ci.push_back(local_stc);

  CorvusGenerator gen;
  const std::string base = "build/corvus_test_output";
  if (!gen.generate(analysis, base, 1, 1)) {
    std::cerr << "CorvusGenerator failed\n";
    return 1;
  }

  std::ifstream ifs(base + "_corvus.json");
  if (!ifs.is_open()) {
    std::cerr << "Missing output json\n";
    return 1;
  }
  std::string json_content((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
  if (json_content.find("top_inputs") == std::string::npos ||
      json_content.find("partitions") == std::string::npos) {
    std::cerr << "Unexpected JSON content\n";
    return 1;
  }

  std::ifstream hfs(base + "_corvus_gen.h");
  if (!hfs.is_open()) {
    std::cerr << "Missing generated header\n";
    return 1;
  }
  std::string header_content((std::istreambuf_iterator<char>(hfs)),
                             std::istreambuf_iterator<char>());
  if (header_content.find("CorvusTopModuleGen") == std::string::npos ||
      header_content.find("CorvusSimWorkerGenP0") == std::string::npos) {
    std::cerr << "Missing expected class definitions\n";
    return 1;
  }

  std::cout << "corvus_generator: PASS\n";
  return 0;
}
