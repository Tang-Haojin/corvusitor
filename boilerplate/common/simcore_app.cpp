#include "simcore_app.h"

void SimCoreApp::loop() {
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

void SimCoreApp::raiseCFinishFlag() {
    if(cFinishFlag == SynctreeEndpoint::FlipFlag::PENDING || cFinishFlag == SynctreeEndpoint::FlipFlag::B_SIDE) {
        cFinishFlag = SynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        cFinishFlag = SynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setCFinishFlag(cFinishFlag);
}

void SimCoreApp::raiseSFinishFlag() {
    if(sFinishFlag == SynctreeEndpoint::FlipFlag::PENDING || sFinishFlag == SynctreeEndpoint::FlipFlag::B_SIDE) {
        sFinishFlag = SynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        sFinishFlag = SynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setSFinishFlag(sFinishFlag);
}

bool SimCoreApp::isMasterSyncFlagRaised() {
    if(synctreeEndpoint->getMasterSyncFlag() == SynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }   
    if(prevMasterSyncFlag == synctreeEndpoint->getMasterSyncFlag()) {
        return false;
    } else {
        prevMasterSyncFlag = synctreeEndpoint->getMasterSyncFlag();
        return true;
    }
}