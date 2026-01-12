#include "corvus_top_module.h"
#include <utility>
#include <iostream>


CorvusTopModule::CorvusTopModule(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints)
    : synctreeEndpoint(masterSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)) {
}

CorvusTopModule::~CorvusTopModule() {
    std::cout << "[CorvusTopModule] Destructor state: "
              << "masterSyncFlag=" << masterSyncFlag.getValue()
              << ", prevSFinishFlag=" << masterSyncFlag.getValue()
              << ", lastStage=" << lastStage
              << ", evalCount=" << evalCount
              << std::endl;

    if (synctreeEndpoint) {
        std::cout << "[CorvusTopModule] Synctree flags at teardown: "
                  << ", simCoreSFinish=" << synctreeEndpoint->getSimCoreSFinishFlag().getValue()
                  << ", mBusClear=" << (synctreeEndpoint->isMBusClear() ? "true" : "false")
                  << ", sBusClear=" << (synctreeEndpoint->isSBusClear() ? "true" : "false")
                  << std::endl;
    } else {
        std::cout << "[CorvusTopModule] Synctree endpoint is null at teardown" << std::endl;
    }
}

void CorvusTopModule::resetSimWorker() {
}

void CorvusTopModule::eval() {
    evalCount++;
    logStage("eval_start");
    sendIAndEOutput();
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear()) {}
    raiseMasterSyncFlag();
    logStage(std::string("master sync flag raised to ") + std::to_string(masterSyncFlag.getValue()));
    logStage("waiting for S finish");
    while(!synctreeEndpoint->isMBusClear() || !allSimCoreSFinish()) {}
    logStage("S finish detected");
    loadOAndEInput();
    logStage("eval_done");
}

void CorvusTopModule::evalE() {
    eHandle->eval();
}

bool CorvusTopModule::allSimCoreCFinish() {
    return false;
}

bool CorvusTopModule::allSimCoreSFinish() {
    if(synctreeEndpoint->getSimCoreSFinishFlag().getValue() == prevSFinishFlag.getValue()) {
        return false;
    } else if (synctreeEndpoint->getSimCoreSFinishFlag().getValue() == 0) {
        // pending state
        return false;
    } else if (synctreeEndpoint->getSimCoreSFinishFlag().getValue() == prevSFinishFlag.nextValue()) {
        prevSFinishFlag.updateToNext();
        return true;
    } else {
        // 丢步，严重错误
        std::cerr << "[CorvusTopModule] Fatal error: SimCore S finish flag jumped from "
                  << static_cast<int>(prevSFinishFlag.getValue()) << " to "
                  << static_cast<int>(synctreeEndpoint->getSimCoreSFinishFlag().getValue())
                  << std::endl;
        exit(1);
    }
}

void CorvusTopModule::raiseMasterSyncFlag() {
    masterSyncFlag.updateToNext();
    synctreeEndpoint->setMasterSyncFlag(masterSyncFlag);
}

void CorvusTopModule::clearMBusRecvBuffer() {
    for (auto endpoint : mBusEndpoints) {
        if (endpoint) {
            endpoint->clearBuffer();
        }
    }
}


void CorvusTopModule::logStage(std::string stageLabel) {
    lastStage = stageLabel;
    printf("TopModule evalCount=%llu stage=%s\n",
           static_cast<unsigned long long>(evalCount),
           stageLabel.c_str());
}
