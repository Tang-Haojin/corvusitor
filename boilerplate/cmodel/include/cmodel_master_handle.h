#ifndef CMODEL_MASTER_HANDLE_H
#define CMODEL_MASTER_HANDLE_H

#include <vector>

#include "idealized_cmodel_bus.h"
#include "master_handle.h"

class CModelMasterHandle : public MasterHandle {
public:
    CModelMasterHandle(ModuleHandle* eModule,
                       MasterSynctreeEndpoint* masterSynctreeEndpoint,
                       std::vector<IdealizedCModelBusEndpoint*> mBusEndpoints,
                       std::vector<IdealizedCModelBusEndpoint*> sBusEndpoints);
    ~CModelMasterHandle() override = default;

    void clearMBusRecvBuffer() override;
    void sendIAndEOutput() override;
    void loadOAndEInput() override;

private:
    std::vector<IdealizedCModelBusEndpoint*> mBusEndpoints;
    std::vector<IdealizedCModelBusEndpoint*> sBusEndpoints;
};

#endif // CMODEL_MASTER_HANDLE_H
