#ifndef CORVUS_SIM_WORKER_H
#define CORVUS_SIM_WORKER_H
#include <vector>

#include "sim_worker.h"
#include "corvus_bus_endpoint.h"
#include "corvus_synctree_endpoint.h"
class CorvusSimWorker : public SimWorker {
    public:
        CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
                        std::vector<CorvusBusEndpoint*> mBusEndpoints,
                        std::vector<CorvusBusEndpoint*> sBusEndpoints);
        void loop() override;
    protected:
        CorvusSimWorkerSynctreeEndpoint* synctreeEndpoint;
        std::vector<CorvusBusEndpoint*> mBusEndpoints;
        std::vector<CorvusBusEndpoint*> sBusEndpoints;
        virtual void loadBusCInputs() = 0;
        virtual void sendCOutputsToBus() = 0;
        virtual void loadSInputs() = 0;
        virtual void sendSOutputs() = 0;
        virtual void loadLocalCInputs() = 0;
    private:
        CorvusSynctreeEndpoint::FlipFlag cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        CorvusSynctreeEndpoint::FlipFlag sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        CorvusSynctreeEndpoint::FlipFlag prevMasterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        void raiseCFinishFlag();
        void raiseSFinishFlag();
        bool isMasterSyncFlagRaised();
};

#endif
