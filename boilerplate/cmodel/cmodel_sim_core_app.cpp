#include "cmodel_sim_core_app.h"

#include <utility>

CModelSimCoreApp::CModelSimCoreApp(ModuleHandle* cModule,
                                   ModuleHandle* sModule,
                                   SimCoreSynctreeEndpoint* simCoreSynctreeEndpoint,
                                   std::vector<IdealizedCModelBusEndpoint*> mBusEndpoints,
                                   std::vector<IdealizedCModelBusEndpoint*> sBusEndpoints)
    : mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {
    this->cModule = cModule;
    this->sModule = sModule;
    this->synctreeEndpoint = simCoreSynctreeEndpoint;
}
