#ifndef MASTER_HANDLE_H
#define MASTER_HANDLE_H
#include "module_handle.h"
#include "synctree_endpoint.h"
class MasterHandle {
public:
    virtual ~MasterHandle() = default;
    void init();
    void eval();
    void evalE();
    protected:
    ModuleHandle* eModule;
    MasterSynctreeEndpoint* synctreeEndpoint;
    private:
    SynctreeEndpoint::FlipFlag prevCFinishFlag = SynctreeEndpoint::FlipFlag::PENDING;
    SynctreeEndpoint::FlipFlag prevSFinishFlag = SynctreeEndpoint::FlipFlag::PENDING;
    SynctreeEndpoint::FlipFlag masterSyncFlag = SynctreeEndpoint::FlipFlag::PENDING;
    bool allSimCoreCFinish();
    bool allSimCoreSFinish();
    virtual void clearMBusRecvBuffer() = 0;
    virtual void sendIAndEOutput() = 0;
    void raiseMasterSyncFlag();
    virtual void loadOAndEInput() = 0;
};
#endif
