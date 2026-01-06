#ifndef CMODEL_SIMCORE_APP_H
#define CMODEL_SIMCORE_APP_H

#include <vector>

#include "idealized_cmodel_bus.h"
#include "sim_core_app.h"

class CModelSimCoreApp : public SimCoreApp {
public:
    CModelSimCoreApp(ModuleHandle* cModule,
                     ModuleHandle* sModule,
                     SimCoreSynctreeEndpoint* simCoreSynctreeEndpoint,
                     std::vector<IdealizedCModelBusEndpoint*> mBusEndpoints,
                     std::vector<IdealizedCModelBusEndpoint*> sBusEndpoints);
    ~CModelSimCoreApp() override = default;

    void loadBusCInputs() override;
    void sendCOutputsToBus() override;
    void loadSInputs() override;
    void sendSOutputs() override;
    void loadLocalCInputs() override;

private:
    std::vector<IdealizedCModelBusEndpoint*> mBusEndpoints;
    std::vector<IdealizedCModelBusEndpoint*> sBusEndpoints;
};

#endif // CMODEL_SIMCORE_APP_H
