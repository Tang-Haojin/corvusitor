#ifndef CONNECTION_ANALYSIS_H
#define CONNECTION_ANALYSIS_H

#include "module_info.h"
#include <map>
#include <string>
#include <vector>

// Lightweight reference to a module port (pointers owned by ModuleInfo)
struct SignalEndpoint {
  const ModuleInfo* module = nullptr;
  const PortInfo* port = nullptr;
};

// Classified connection describing one driver -> one or more receivers
struct ClassifiedConnection {
  std::string port_name;
  int width = 0;
  PortWidthType width_type = PortWidthType::VL_8;
  SignalEndpoint driver;
  std::vector<SignalEndpoint> receivers;
};

// Partition-scoped group of classified connections
struct PartitionConnections {
  std::vector<ClassifiedConnection> local_cts_to_si;   // comb_i -> seq_i
  std::vector<ClassifiedConnection> local_stc_to_ci;   // seq_i -> comb_i
  std::vector<ClassifiedConnection> remote_s_to_c;     // seq_i -> comb_j (i != j)
};

// Full connection analysis result
struct ConnectionAnalysis {
  std::vector<ClassifiedConnection> top_inputs;     // I
  std::vector<ClassifiedConnection> top_outputs;    // O
  std::vector<ClassifiedConnection> external_inputs;  // Ei (comb -> external)
  std::vector<ClassifiedConnection> external_outputs; // Eo (external -> comb)
  std::map<int, PartitionConnections> partitions;   // keyed by partition id
  std::vector<std::string> warnings;                // non-fatal constraint issues
};

#endif // CONNECTION_ANALYSIS_H
