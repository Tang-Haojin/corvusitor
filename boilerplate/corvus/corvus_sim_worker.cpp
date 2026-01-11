#include "corvus_sim_worker.h"
#include <utility>
#include <cstdio>

CorvusSimWorker::CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints,
                                 std::vector<CorvusBusEndpoint*> sBusEndpoints)
    : synctreeEndpoint(simCoreSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {}

void CorvusSimWorker::loop() {
    printf("SimWorker loop started\n");
    while(1) {
        loadRemoteCInputs();
        printf("SimWorker loaded remote C inputs\n");
        cModule->eval();
        printf("SimWorker C module evaluated\n");
        sendRemoteCOutputs();
        printf("SimWorker sent remote C outputs\n");
        raiseCFinishFlag();
        printf("SimWorker raised C finish flag\n");
        loadSInputs();
        printf("SimWorker loaded S inputs\n");
        sModule->eval();
        printf("SimWorker S module evaluated\n");
        sendRemoteSOutputs();
        printf("SimWorker sent remote S outputs\n");
        raiseSFinishFlag();
        printf("SimWorker raised S finish flag\n");
        loadLocalCInputs();
        printf("SimWorker loaded local C inputs\n");
        while(!isMasterSyncFlagRaised());
    }
}

void CorvusSimWorker::raiseCFinishFlag() {
    if(cFinishFlag == CorvusSynctreeEndpoint::FlipFlag::PENDING || cFinishFlag == CorvusSynctreeEndpoint::FlipFlag::B_SIDE) {
        cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setCFinishFlag(cFinishFlag);
}

void CorvusSimWorker::raiseSFinishFlag() {
    if(sFinishFlag == CorvusSynctreeEndpoint::FlipFlag::PENDING || sFinishFlag == CorvusSynctreeEndpoint::FlipFlag::B_SIDE) {
        sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setSFinishFlag(sFinishFlag);
}

bool CorvusSimWorker::isMasterSyncFlagRaised() {
    if(synctreeEndpoint->getMasterSyncFlag() == CorvusSynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }   
    if(prevMasterSyncFlag == synctreeEndpoint->getMasterSyncFlag()) {
        return false;
    } else {
        prevMasterSyncFlag = synctreeEndpoint->getMasterSyncFlag();
        return true;
    }
}
