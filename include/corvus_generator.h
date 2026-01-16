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
  struct SlotRecvRecord {
    std::string portName;
    int slotId = 0;
    int bitOffset = 0;
  };

  struct SlotSendRecord {
    std::string portName;
    int bitOffset = 0;
    int targetId = 0;
    int slotId = 0;
  };

  struct CopyRecord {
    std::string portName;
  };

  struct TopModulePlan {
    std::vector<SlotSendRecord> input;
    std::vector<SlotRecvRecord> output;
    std::vector<SlotRecvRecord> externalInput;
    std::vector<SlotSendRecord> externalOutput;
  };

  struct SimWorkerPlan {
    std::vector<SlotRecvRecord> loadMBusCInputs;
    std::vector<SlotRecvRecord> loadSBusCInputs;
    std::vector<SlotSendRecord> sendMBusCOutputs;
    std::vector<CopyRecord> copySInputs;
    std::vector<SlotSendRecord> sendSBusSOutputs;
    std::vector<CopyRecord> copyLocalCInputs;
  };

  struct CorvusBusPlan {
    TopModulePlan topModulePlan;
    std::map<int, SimWorkerPlan> simWorkerPlans;
  };

  bool generate(const ConnectionAnalysis& analysis,
                const std::string& output_base,
                int mbus_count,
                int sbus_count) override;

private:
  bool write_connection_analysis_json(const ConnectionAnalysis& analysis,
                                      const std::string& output_base) const;
  bool write_bus_plan_json(const CorvusBusPlan& plan,
                           const std::vector<std::string>& warnings,
                           const std::string& output_base) const;
};

#endif // CORVUS_GENERATOR_H
