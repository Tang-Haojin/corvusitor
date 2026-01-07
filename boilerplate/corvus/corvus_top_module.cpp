#include "corvus_top_module.h"
#include <utility>

CorvusTopModule::CorvusTopModule(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints,
                                 std::vector<CorvusBusEndpoint*> sBusEndpoints)
    : synctreeEndpoint(masterSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {
}

void CorvusTopModule::resetSimWorker() {
    synctreeEndpoint->forceSimCoreReset();
    while(!synctreeEndpoint->isMBusClear() ||
          !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    clearMBusRecvBuffer();
}

void CorvusTopModule::eval() {
    sendIAndEOutput();
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    raiseMasterSyncFlag();
    while(!synctreeEndpoint->isMBusClear() || !allSimCoreCFinish()) {}
    loadOAndEInput();
}

void CorvusTopModule::evalE() {
    eHandle->eval();
}

bool CorvusTopModule::allSimCoreCFinish() {
    if(synctreeEndpoint->getSimCoreCFinishFlag() == CorvusSynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }
    if(prevCFinishFlag == synctreeEndpoint->getSimCoreCFinishFlag()) {
        return false;
    } else {
        prevCFinishFlag = synctreeEndpoint->getSimCoreCFinishFlag();
        return true;
    }
}

bool CorvusTopModule::allSimCoreSFinish() {
    if(synctreeEndpoint->getSimCoreSFinishFlag() == CorvusSynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }
    if(prevSFinishFlag == synctreeEndpoint->getSimCoreSFinishFlag()) {
        return false;
    } else {
        prevSFinishFlag = synctreeEndpoint->getSimCoreSFinishFlag();
        return true;
    }
}

void CorvusTopModule::raiseMasterSyncFlag() {
    if(masterSyncFlag == CorvusSynctreeEndpoint::FlipFlag::PENDING || masterSyncFlag == CorvusSynctreeEndpoint::FlipFlag::B_SIDE) {
        masterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        masterSyncFlag = CorvusSynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setMasterSyncFlag(masterSyncFlag);
}

void CorvusTopModule::clearMBusRecvBuffer() {
    for (auto endpoint : mBusEndpoints) {
        if (endpoint) {
            endpoint->clearBuffer();
        }
    }
}
