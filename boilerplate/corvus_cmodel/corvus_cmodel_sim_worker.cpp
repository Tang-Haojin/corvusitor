#include "corvus_cmodel_sim_worker.h"

#include <utility>

CorvusCModelSimWorker::CorvusCModelSimWorker(ModuleHandle* cModule,
                                             ModuleHandle* sModule,
                                             std::shared_ptr<CorvusSimWorkerSynctreeEndpoint> simCoreSynctreeEndpoint,
                                             std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints,
                                             std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints)
    : mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {
    this->cModule = cModule;
    this->sModule = sModule;
    this->synctreeEndpoint = simCoreSynctreeEndpoint;
}
