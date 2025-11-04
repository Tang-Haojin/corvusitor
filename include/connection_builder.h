#ifndef CONNECTION_BUILDER_H
#define CONNECTION_BUILDER_H

#include "module_info.h"
#include <vector>
#include <map>
#include <string>

// Port connection information
struct PortConnection {
  std::string port_name;           // Port name
  const ModuleInfo* driver_module; // Module driving this port (output)
  std::vector<const ModuleInfo*> receiver_modules; // Modules receiving this port (input)
  int width;                       // Port bit width
  PortWidthType width_type;        // Port type

  bool is_top_level_input;         // Is top-level input port (no driver)
  bool is_top_level_output;        // Is top-level output port (no receiver)
};

// Port group information (supports multiple drivers)
struct PortGroup {
  std::string port_name;
  std::vector<const PortInfo*> driver_ports;      // Driver port list (supports multiple COMB outputs)
  std::vector<const ModuleInfo*> driver_modules;  // Driver module list (corresponds to driver_ports)
  std::vector<const PortInfo*> receiver_ports;    // Receiver port list
  std::vector<const ModuleInfo*> receiver_modules; // Receiver module list
};

// Connection builder
class ConnectionBuilder {
public:
  ConnectionBuilder() = default;

  // Build connection relationships between all modules
  std::vector<PortConnection> build(const std::vector<ModuleInfo>& modules);

private:
  // Group all ports by port name
  std::map<std::string, PortGroup>
  group_ports_by_name(const std::vector<ModuleInfo>& modules);

  // Validate port connection legality
  bool validate_connection(
    const PortGroup& group,
    std::string& error_msg
  );

  // Check if two ports are bit-width compatible
  bool is_width_compatible(const PortInfo& p1, const PortInfo& p2);
};

#endif // CONNECTION_BUILDER_H
