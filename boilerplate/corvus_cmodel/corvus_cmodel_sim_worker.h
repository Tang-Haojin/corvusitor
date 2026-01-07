#ifndef CORVUS_CMODEL_SIM_WORKER_H
#define CORVUS_CMODEL_SIM_WORKER_H

#include <memory>
#include <vector>

#include "corvus_cmodel_idealized_bus.h"
#include "corvus_sim_worker.h"

class CorvusCModelSimWorker : public CorvusSimWorker {
public:
    CorvusCModelSimWorker(ModuleHandle* cModule,
                          ModuleHandle* sModule,
                          std::shared_ptr<CorvusSimWorkerSynctreeEndpoint> simCoreSynctreeEndpoint,
                          std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints,
                          std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints);
    ~CorvusCModelSimWorker() override = default;

    void loadBusCInputs() override;
    void sendCOutputsToBus() override;
    void loadSInputs() override;
    void sendSOutputs() override;
    void loadLocalCInputs() override;

private:
    std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints;
    std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints;
};

#endif // CORVUS_CMODEL_SIM_WORKER_H
