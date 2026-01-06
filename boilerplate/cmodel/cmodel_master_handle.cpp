#include "cmodel_master_handle.h"

#include <utility>

CModelMasterHandle::CModelMasterHandle(ModuleHandle* eModule,
                                       MasterSynctreeEndpoint* masterSynctreeEndpoint,
                                       std::vector<IdealizedCModelBusEndpoint*> mBusEndpoints,
                                       std::vector<IdealizedCModelBusEndpoint*> sBusEndpoints)
    : mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {
    this->eModule = eModule;
    this->synctreeEndpoint = masterSynctreeEndpoint;
}
