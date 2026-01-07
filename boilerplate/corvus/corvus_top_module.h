#ifndef CORVUS_TOP_MODULE_H
#define CORVUS_TOP_MODULE_H

#include <memory>

#include "top_module.h"
#include "corvus_synctree_endpoint.h"

class CorvusTopModule : public TopModule {
public:
    ~CorvusTopModule() override = default;
    void init() override;
    void eval() override;
    void evalE() override;

protected:
    std::shared_ptr<CorvusTopSynctreeEndpoint> synctreeEndpoint = nullptr;

private:
    CorvusSynctreeEndpoint::FlipFlag prevCFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
    CorvusSynctreeEndpoint::FlipFlag prevSFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
    CorvusSynctreeEndpoint::FlipFlag masterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;

    bool allSimCoreCFinish();
    bool allSimCoreSFinish();
    virtual void clearMBusRecvBuffer() = 0;
    virtual void sendIAndEOutput() = 0;
    void raiseMasterSyncFlag();
    virtual void loadOAndEInput() = 0;
};

#endif
