#include "corvus_generator.h"
#include <fstream>
#include <iostream>

namespace {

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

} // namespace

bool CorvusGenerator::generate(const ConnectionAnalysis& analysis,
                               const std::string& output_base) {
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
  return true;
}
