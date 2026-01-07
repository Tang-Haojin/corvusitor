#ifndef CORVUS_CMODEL_TOP_MODULE_H
#define CORVUS_CMODEL_TOP_MODULE_H

#include <memory>
#include <vector>

#include "corvus_top_module.h"
#include "corvus_cmodel_idealized_bus.h"

class CorvusCModelTopModule : public CorvusTopModule {
public:
    CorvusCModelTopModule(ModuleHandle* eModule,
                          std::shared_ptr<CorvusTopSynctreeEndpoint> masterSynctreeEndpoint,
                          std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints,
                          std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints);
    ~CorvusCModelTopModule() override = default;

    void clearMBusRecvBuffer() override;
    void sendIAndEOutput() override;
    void loadOAndEInput() override;

private:
    std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> mBusEndpoints;
    std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> sBusEndpoints;
};

#endif // CORVUS_CMODEL_TOP_MODULE_H
