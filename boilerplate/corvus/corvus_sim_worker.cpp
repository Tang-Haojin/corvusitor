#include "corvus_sim_worker.h"

void CorvusSimWorker::loop() {
    while(1) {
        loadBusCInputs();
        cModule->eval();
        sendCOutputsToBus();
        raiseCFinishFlag();
        loadSInputs();
        sModule->eval();
        sendSOutputs();
        raiseSFinishFlag();
        loadLocalCInputs();
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
