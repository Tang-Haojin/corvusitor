#include "corvus_cmodel_top_module.h"

#include <utility>

CorvusCModelTopModule::CorvusCModelTopModule(ModuleHandle* eModule,
                                             std::shared_ptr<CorvusTopSynctreeEndpoint> masterSynctreeEndpoint,
                                             std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints,
                                             std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints)
    : mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {
    this->eHandle = eModule;
    this->synctreeEndpoint = masterSynctreeEndpoint;
}
