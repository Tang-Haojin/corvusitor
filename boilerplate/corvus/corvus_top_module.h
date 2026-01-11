#ifndef CORVUS_TOP_MODULE_H
#define CORVUS_TOP_MODULE_H

#include <vector>

#include "top_module.h"
#include "corvus_bus_endpoint.h"
#include "corvus_synctree_endpoint.h"

class CorvusTopModule : public TopModule {
public:
    CorvusTopModule(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,
                    std::vector<CorvusBusEndpoint*> mBusEndpoints);
    ~CorvusTopModule() override;
    void resetSimWorker() override;
    void eval() override;
    void evalE() override;

protected:
    CorvusTopSynctreeEndpoint* synctreeEndpoint = nullptr;
    std::vector<CorvusBusEndpoint*> mBusEndpoints;
    virtual void sendIAndEOutput() = 0;
    virtual void loadOAndEInput() = 0;

private:
    CorvusSynctreeEndpoint::FlipFlag prevCFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
    CorvusSynctreeEndpoint::FlipFlag prevSFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
    CorvusSynctreeEndpoint::FlipFlag masterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
    const char* lastStage = "init";
    uint64_t evalCount = 0;

    bool allSimCoreCFinish();
    bool allSimCoreSFinish();
    void raiseMasterSyncFlag();
    void clearMBusRecvBuffer();
    void setStage(const char* stage);
    void logStage(const char* stageLabel, uint64_t iter);

    int debugEvalCnt = 0;
};

#endif
