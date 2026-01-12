#ifndef CORVUS_TOP_MODULE_H
#define CORVUS_TOP_MODULE_H

#include <vector>
#include <string>

#include "top_module.h"
#include "corvus_bus_endpoint.h"
#include "corvus_synctree_endpoint.h"

class CorvusTopModule : public TopModule {
public:
    CorvusTopModule(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,
                    std::vector<CorvusBusEndpoint*> mBusEndpoints);
    ~CorvusTopModule() override;
    void prepareSimWorker() override;
    void eval() override;
    void evalE() override;

protected:
    CorvusTopSynctreeEndpoint* synctreeEndpoint = nullptr;
    std::vector<CorvusBusEndpoint*> mBusEndpoints;
    virtual void sendIAndEOutput() = 0;
    virtual void loadOAndEInput() = 0;

private:
    CorvusSynctreeEndpoint::ValueFlag prevCFinishFlag;
    CorvusSynctreeEndpoint::ValueFlag prevSFinishFlag;
    CorvusSynctreeEndpoint::ValueFlag masterSyncFlag;
    std::string lastStage = "init";
    uint64_t evalCount = 0;

    bool allSimCoreCFinish();
    bool allSimCoreSFinish();
    void raiseMasterSyncFlag();
    void clearMBusRecvBuffer();
    void logStage(std::string stageLabel);
};

#endif
