#include "corvus_top_module.h"
#include <utility>
#include <iostream>
CorvusTopModule::CorvusTopModule(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints)
    : synctreeEndpoint(masterSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)) {
}

void CorvusTopModule::resetSimWorker() {
    synctreeEndpoint->forceSimCoreReset();
    while(!synctreeEndpoint->isMBusClear() ||
          !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    std::cout << "All simWorker S Finished" << std::endl;
    clearMBusRecvBuffer();
    prevSFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
}

void CorvusTopModule::eval() {
    std::cout << "Top Module: " << debugEvalCnt++ << " eval start" << std::endl;
    sendIAndEOutput();
    std::cout << "Top Module Send I and E Output" << std::endl;
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    std::cout << "Top Module：All simWorker S Finished" << std::endl;
    raiseMasterSyncFlag();
    while(!synctreeEndpoint->isMBusClear() || !allSimCoreCFinish()) {}
    std::cout << "Top Module: All simWorker C Finished" << std::endl;
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
