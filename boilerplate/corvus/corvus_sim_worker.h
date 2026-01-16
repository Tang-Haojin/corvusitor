#ifndef CORVUS_SIM_WORKER_H
#define CORVUS_SIM_WORKER_H
#include <string>
#include <vector>

#include "sim_worker.h"
#include "corvus_bus_endpoint.h"
#include "corvus_synctree_endpoint.h"
class CorvusSimWorker : public SimWorker {
    public:
        ~CorvusSimWorker() override;
        CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simWorkerSynctreeEndpoint,
                        std::vector<CorvusBusEndpoint*> mBusEndpoints,
                        std::vector<CorvusBusEndpoint*> sBusEndpoints);
        void loop() override;
        void stop();
        const std::string& name() const { return workerName; }
        void setName(std::string name);
    protected:
        CorvusSimWorkerSynctreeEndpoint* synctreeEndpoint;
        std::vector<CorvusBusEndpoint*> mBusEndpoints;
        std::vector<CorvusBusEndpoint*> sBusEndpoints;
        uint64_t loopCount = 0;
        virtual void loadMBusCInputs()=0;
        virtual void loadSBusCInputs()=0;
        virtual void sendMBusCOutputs()=0;
        virtual void copySInputs()=0;
        virtual void sendSBusSOutputs()=0;
        virtual void copyLocalCInputs()=0;
    private:
        // Compatibility helpers for the newer sync flow; implemented using legacy hooks.
        bool hasStartFlagSeen();
        bool isTopSyncFlagRaised();
        void raiseSimWorkerInputReadyFlag();
        bool isTopAllowSOutputFlagRaised();
        void raiseSimWorkerSyncFlag();
        CorvusSynctreeEndpoint::ValueFlag simWorkerInputReadyFlag;
        CorvusSynctreeEndpoint::ValueFlag simWorkerSyncFlag;
        CorvusSynctreeEndpoint::ValueFlag prevTopSyncFlag;
        CorvusSynctreeEndpoint::ValueFlag prevTopAllowSOutputFlag;
        std::string lastStage = "init";
        std::string workerName;
        bool loopContinue;
        void logStage(std::string stageLabel);
};

#endif
