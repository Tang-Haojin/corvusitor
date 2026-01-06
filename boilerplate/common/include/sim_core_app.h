#ifndef SIMCORE_APP_H
#define SIMCORE_APP_H
#include "module_handle.h"
#include "synctree_endpoint.h"
class SimCoreApp {
    public:
        virtual ~SimCoreApp() = default;
        void loop();
    protected:
        ModuleHandle* cModule;
        ModuleHandle* sModule;
        SimCoreSynctreeEndpoint* synctreeEndpoint;
    private:
        SynctreeEndpoint::FlipFlag cFinishFlag = SynctreeEndpoint::FlipFlag::PENDING;
        SynctreeEndpoint::FlipFlag sFinishFlag = SynctreeEndpoint::FlipFlag::PENDING;
        SynctreeEndpoint::FlipFlag prevMasterSyncFlag = SynctreeEndpoint::FlipFlag::PENDING;
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