#include "../include/corvus_generator.h"
#include <fstream>
#include <iostream>
#include <string>

// Simple smoke test: emit a corvus JSON from a synthetic analysis graph.
int main() {
  ConnectionAnalysis analysis;
  // Populate minimal topology
  ClassifiedConnection top_in;
  top_in.port_name = "in0";
  top_in.width = 1;
  analysis.top_inputs.push_back(top_in);

  ClassifiedConnection top_out;
  top_out.port_name = "out0";
  top_out.width = 1;
  analysis.top_outputs.push_back(top_out);

  ClassifiedConnection local;
  local.port_name = "sig";
  local.width = 8;
  local.width_type = PortWidthType::VL_8;
  analysis.partitions[0].local_cts_to_si.push_back(local);

  CorvusGenerator gen;
  const std::string base = "build/corvus_test_output";
  if (!gen.generate(analysis, base)) {
    std::cerr << "CorvusGenerator failed\n";
    return 1;
  }

  std::ifstream ifs(base + "_corvus.json");
  if (!ifs.is_open()) {
    std::cerr << "Missing output json\n";
    return 1;
  }
  std::string content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
  if (content.find("top_inputs") == std::string::npos ||
      content.find("partitions") == std::string::npos) {
    std::cerr << "Unexpected JSON content\n";
    return 1;
  }

  std::cout << "corvus_generator: PASS\n";
  return 0;
}
