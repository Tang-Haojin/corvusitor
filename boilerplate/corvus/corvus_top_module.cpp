#include "corvus_top_module.h"
#include <utility>
#include <iostream>
#include <cstdint>


CorvusTopModule::CorvusTopModule(CorvusTopSynctreeEndpoint* topSynctreeEndpoint,
                                                                 std::vector<CorvusBusEndpoint*> mBusEndpoints)
        : synctreeEndpoint(topSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)) {
}

CorvusTopModule::~CorvusTopModule() {
    std::cout << "[CorvusTopModule] Destructor state: "
              << "topSyncFlag=" << static_cast<unsigned int>(topSyncFlag.getValue())
              << ", prevSFinishFlag=" << static_cast<unsigned int>(prevSFinishFlag.getValue())
              << ", lastStage=" << lastStage
              << ", evalCount=" << evalCount
              << std::endl;

    if (synctreeEndpoint) {
        std::cout << "[CorvusTopModule] Synctree flags at teardown: "
                  << ", simWorkerSFinish=" << static_cast<unsigned int>(synctreeEndpoint->getSimWorkerSFinishFlag().getValue())
                  << ", mBusClear=" << (synctreeEndpoint->isMBusClear() ? "true" : "false")
                  << ", sBusClear=" << (synctreeEndpoint->isSBusClear() ? "true" : "false")
                  << std::endl;
    } else {
        std::cout << "[CorvusTopModule] Synctree endpoint is null at teardown" << std::endl;
    }
}

void CorvusTopModule::prepareSimWorker() {
    synctreeEndpoint->setSimWorkerStartFlag(CorvusSynctreeEndpoint::ValueFlag::START_GUARD);
}

void CorvusTopModule::eval() {
    evalCount++;
    logStage("eval_start");
    sendIAndEOutput();
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear()) {}
    raiseTopSyncFlag();
    logStage(std::string("top sync flag raised to ") + std::to_string(topSyncFlag.getValue()));
    logStage("waiting for S finish");
    while(!synctreeEndpoint->isMBusClear() || !allSimWorkerSFinish()) {}
    logStage("S finish detected");
    loadOAndEInput();
    logStage("eval_done");
}

void CorvusTopModule::evalE() {
    eModule->eval();
}

bool CorvusTopModule::allSimWorkerSFinish() {
    if(synctreeEndpoint->getSimWorkerSFinishFlag().getValue() == prevSFinishFlag.getValue()) {
        return false;
    } else if (synctreeEndpoint->getSimWorkerSFinishFlag().getValue() == 0) {
        // pending state
        return false;
    } else if (synctreeEndpoint->getSimWorkerSFinishFlag().getValue() == prevSFinishFlag.nextValue()) {
        prevSFinishFlag.updateToNext();
        return true;
    } else {
        // 丢步，严重错误
        std::cerr << "[CorvusTopModule] Fatal error: SimWorker S finish flag jumped from "
                  << static_cast<int>(prevSFinishFlag.getValue()) << " to "
                  << static_cast<int>(synctreeEndpoint->getSimWorkerSFinishFlag().getValue())
                  << std::endl;
        exit(1);
    }
}

void CorvusTopModule::raiseTopSyncFlag() {
    topSyncFlag.updateToNext();
    synctreeEndpoint->setTopSyncFlag(topSyncFlag);
}

void CorvusTopModule::clearMBusRecvBuffer() {
    for (auto endpoint : mBusEndpoints) {
        if (endpoint) {
            endpoint->clearBuffer();
        }
    }
}


void CorvusTopModule::logStage(std::string stageLabel) {
    // lastStage = stageLabel;
    // printf("TopModule evalCount=%llu stage=%s\n",
    //        static_cast<unsigned long long>(evalCount),
    //        stageLabel.c_str());
}
