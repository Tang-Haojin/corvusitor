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
              << ", topAllowSOutputFlag=" << static_cast<unsigned int>(topAllowSOutputFlag.getValue())
              << ", prevSimWorkerInputReadyFlag=" << static_cast<unsigned int>(prevSimWorkerInputReadyFlag.getValue())
              << ", prevSimWorkerSyncFlag=" << static_cast<unsigned int>(prevSimWorkerSyncFlag.getValue())
              << ", lastStage=" << lastStage
              << ", evalCount=" << evalCount
              << std::endl;

    if (synctreeEndpoint) {
        std::cout << "[CorvusTopModule] Synctree flags at teardown: "
                  << ", simWorkerInputReady=" << static_cast<unsigned int>(synctreeEndpoint->getSimWorkerInputReadyFlag().getValue())
                  << ", simWorkerSync=" << static_cast<unsigned int>(synctreeEndpoint->getSimWorkerSyncFlag().getValue())
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
    logStage("waiting for bus clear");
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear()) {}
    logStage("raise top sync flag");
    raiseTopSyncFlag();
    logStage(std::string("top sync flag raised to ") + std::to_string(topSyncFlag.getValue()));
    logStage("waiting for sim worker input ready");
    while(!isSimWorkerInputReadyFlagRaised()) {}
    logStage(std::string("get sim worker input ready flag as ") + std::to_string(prevSimWorkerInputReadyFlag.getValue()));
    logStage("raise top allow S output flag");
    raiseTopAllowSOutputFlag();
    logStage(std::string("top allow S output flag raised to ") + std::to_string(topAllowSOutputFlag.getValue()));
    logStage("waiting for S finish");
    while(!synctreeEndpoint->isMBusClear() || !isSimWorkerSyncFlagRaised()) {}
    logStage(std::string("get sim worker sync flag as ") + std::to_string(prevSimWorkerSyncFlag.getValue()));
    logStage("S finish detected");
    loadOAndEInput();
    logStage("eval_done");
}

void CorvusTopModule::evalE() {
    eModule->eval();
}

bool CorvusTopModule::isSimWorkerSyncFlagRaised() {
    if(synctreeEndpoint->getSimWorkerSyncFlag().getValue() == prevSimWorkerSyncFlag.getValue()) {
        return false;
    } else if (synctreeEndpoint->getSimWorkerSyncFlag().getValue() == 0) {
        // pending state
        return false;
    } else if (synctreeEndpoint->getSimWorkerSyncFlag().getValue() == prevSimWorkerSyncFlag.nextValue()) {
        prevSimWorkerSyncFlag.updateToNext();
        return true;
    } else {
        // 丢步，严重错误
        while(1) {
            std::cerr << "[CorvusTopModule] Fatal error: SimWorkerSyncFlag finish flag jumped from "
                    << static_cast<int>(prevSimWorkerSyncFlag.getValue()) << " to "
                    << static_cast<int>(synctreeEndpoint->getSimWorkerSyncFlag().getValue())
                    << std::endl;
        }
    }
}

bool CorvusTopModule::isSimWorkerInputReadyFlagRaised() {
    if(synctreeEndpoint->getSimWorkerInputReadyFlag().getValue() == prevSimWorkerInputReadyFlag.getValue()) {
        return false;
    } else if (synctreeEndpoint->getSimWorkerInputReadyFlag().getValue() == 0) {
        // pending state
        return false;
    } else if (synctreeEndpoint->getSimWorkerInputReadyFlag().getValue() == prevSimWorkerInputReadyFlag.nextValue()) {
        prevSimWorkerInputReadyFlag.updateToNext();
        return true;
    } else {
        // 丢步，严重错误
        while(1) {
            std::cerr << "[CorvusTopModule] Fatal error: SimWorkerInputReadyFlag finish flag jumped from "
                    << static_cast<int>(prevSimWorkerInputReadyFlag.getValue()) << " to "
                    << static_cast<int>(synctreeEndpoint->getSimWorkerInputReadyFlag().getValue())
                    << std::endl;
        }
    }
}

void CorvusTopModule::raiseTopSyncFlag() {
    topSyncFlag.updateToNext();
    synctreeEndpoint->setTopSyncFlag(topSyncFlag);
}

void CorvusTopModule::raiseTopAllowSOutputFlag() {
    topAllowSOutputFlag.updateToNext();
    synctreeEndpoint->setTopAllowSOutputFlag(topAllowSOutputFlag);
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
