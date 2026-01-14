#ifndef CORVUS_GENERATOR_H
#define CORVUS_GENERATOR_H

#include "code_generator.h"
#include "connection_analysis.h"
#include <map>
#include <string>
#include <vector>

// CorvusTargetGenerator: emits connection analysis in a corvus-oriented format.
class CorvusGenerator : public CodeGenerator::TargetGenerator {
public:
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

  struct SlotRecord {
    int slot_id = 0;
    int bit_offset = 0;
  };

  struct RecvSignal {
    SignalRef sig;
    bool from_external = false;
    bool to_external = false;
    bool via_sbus = false;
    std::vector<SlotRecord> slots;
  };

  struct RecvPlan {
    int pid = -1; // -1 reserved for Top
    std::vector<RecvSignal> signals;
    int slot_bits = 32;
  };

  struct WorkerPlan {
    int pid = -1;
    const ModuleInfo* comb = nullptr;
    const ModuleInfo* seq = nullptr;
    RecvPlan recv_plan;
    std::vector<ClassifiedConnection> local_cts;
    std::vector<ClassifiedConnection> local_stc;
  };

  struct AddressPlan {
    const ModuleInfo* external_mod = nullptr;
    std::map<std::string, SignalRef> top_inputs;
    std::map<std::string, SignalRef> top_outputs;
    RecvPlan top_recv_plan;
    std::map<int, WorkerPlan> workers;
    int mbus_endpoint_count = 1;
    int sbus_endpoint_count = 1;
  };

  bool generate(const ConnectionAnalysis& analysis,
                const std::string& output_base,
                int mbus_count,
                int sbus_count) override;

private:
  class SlotAddressSpace {
  public:
    SlotAddressSpace() = default;
    void assign_slots(std::vector<RecvSignal>& slots);
    int total_slots() const { return next_slot_; }
    int slot_bits() const;
    int required_bus_count() const { return max_bus_index_ + 1; }

  private:
    void assign_slots_impl(std::vector<RecvSignal>& slots);

    int next_slot_ = 0;
    int max_bus_index_ = 0;
  };

  AddressPlan build_address_plan(const ConnectionAnalysis& analysis) const;
  bool write_plan_json(const ConnectionAnalysis& analysis,
                       const AddressPlan& plan,
                       const std::string& output_base) const;
};

#endif // CORVUS_GENERATOR_H
