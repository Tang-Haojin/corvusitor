#ifndef CORVUS_SIM_WORKER_H
#define CORVUS_SIM_WORKER_H
#include <memory>

#include "sim_worker.h"
#include "corvus_synctree_endpoint.h"
class CorvusSimWorker : public SimWorker {
    public:
        virtual ~CorvusSimWorker() = default;
        void loop() override;
    protected:
        std::shared_ptr<CorvusSimWorkerSynctreeEndpoint> synctreeEndpoint;
    private:
        CorvusSynctreeEndpoint::FlipFlag cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        CorvusSynctreeEndpoint::FlipFlag sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        CorvusSynctreeEndpoint::FlipFlag prevMasterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
        virtual void loadBusCInputs() = 0;
        virtual void sendCOutputsToBus() = 0;
        void raiseCFinishFlag();
        virtual void loadSInputs() = 0;
        virtual void sendSOutputs() = 0;
        void raiseSFinishFlag();
        virtual void loadLocalCInputs() = 0;
        bool isMasterSyncFlagRaised();
};

#endif
