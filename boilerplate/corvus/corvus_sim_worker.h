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
        CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
                        std::vector<CorvusBusEndpoint*> mBusEndpoints,
                        std::vector<CorvusBusEndpoint*> sBusEndpoints,
                        std::string workerName);
        void loop() override;
        const std::string& name() const { return workerName; }
        void setName(std::string name);
    protected:
        void setGeneratedName(std::string name);
        CorvusSimWorkerSynctreeEndpoint* synctreeEndpoint;
        std::vector<CorvusBusEndpoint*> mBusEndpoints;
        std::vector<CorvusBusEndpoint*> sBusEndpoints;
        virtual void loadRemoteCInputs() = 0;
        virtual void sendRemoteCOutputs() = 0;
        virtual void loadSInputs() = 0;
        virtual void sendRemoteSOutputs() = 0;
        virtual void loadLocalCInputs() = 0;
    private:
        CorvusSynctreeEndpoint::FlipFlag cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        CorvusSynctreeEndpoint::FlipFlag sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        CorvusSynctreeEndpoint::FlipFlag prevMasterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        const char* lastStage = "init";
        uint64_t loopCount = 0;
        std::string workerName;
        void ensureWorkerName();
        void raiseCFinishFlag();
        void raiseSFinishFlag();
        bool isMasterSyncFlagRaised();
        void setStage(const char* stage);
        void logStage(const char* stageLabel, uint64_t iter);
};

#endif
