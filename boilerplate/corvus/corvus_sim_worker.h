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
        CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
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
        virtual void loadRemoteCInputs() = 0;
        virtual void sendRemoteCOutputs() = 0;
        virtual void loadSInputs() = 0;
        virtual void sendRemoteSOutputs() = 0;
        virtual void loadLocalCInputs() = 0;
    private:
        CorvusSynctreeEndpoint::ValueFlag cFinishFlag;
        CorvusSynctreeEndpoint::ValueFlag sFinishFlag;
        CorvusSynctreeEndpoint::ValueFlag prevMasterSyncFlag;
        std::string lastStage = "init";
        uint64_t loopCount = 0;
        std::string workerName;
        bool loopContinue;
        void raiseCFinishFlag();
        void raiseSFinishFlag();
        bool hasStartFlagSeen();
        bool isMasterSyncFlagRaised();
        void logStage(std::string stageLabel);
};

#endif
