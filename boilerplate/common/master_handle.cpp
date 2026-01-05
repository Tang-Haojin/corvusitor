#include "master_handle.h"

void MasterHandle::init() {
    synctreeEndpoint->forceSimCoreReset();
    while(!synctreeEndpoint->isMBusClear() ||
          !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    clearMBusRecvBuffer();
}

void MasterHandle::eval() {
    sendIAndEOutput();
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    raiseMasterSyncFlag();
    while(!synctreeEndpoint->isMBusClear() || !allSimCoreCFinish()) {}
    loadOAndEInput();
}

void MasterHandle::evalE() {
    eModule->eval();
}

bool MasterHandle::allSimCoreCFinish() {
    if(synctreeEndpoint->getSimCoreCFinishFlag() == SynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }
    if(prevCFinishFlag == synctreeEndpoint->getSimCoreCFinishFlag()) {
        return false;
    } else {
        prevCFinishFlag = synctreeEndpoint->getSimCoreCFinishFlag();
        return true;
    }
}

bool MasterHandle::allSimCoreSFinish() {
    if(synctreeEndpoint->getSimCoreSFinishFlag() == SynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }
    if(prevSFinishFlag == synctreeEndpoint->getSimCoreSFinishFlag()) {
        return false;
    } else {
        prevSFinishFlag = synctreeEndpoint->getSimCoreSFinishFlag();
        return true;
    }
}

void MasterHandle::raiseMasterSyncFlag() {
    if(masterSyncFlag == SynctreeEndpoint::FlipFlag::PENDING || masterSyncFlag == SynctreeEndpoint::FlipFlag::B_SIDE) {
        masterSyncFlag = SynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        masterSyncFlag = SynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setMasterSyncFlag(masterSyncFlag);
}
