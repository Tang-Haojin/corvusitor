#include "corvus_sim_worker.h"
#include <cstdio>
#include <iostream>
#include <thread>
#include <utility>


CorvusSimWorker::CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simWorkerSynctreeEndpoint,
                                                                 std::vector<CorvusBusEndpoint*> mBusEndpoints,
                                                                 std::vector<CorvusBusEndpoint*> sBusEndpoints)
    : synctreeEndpoint(simWorkerSynctreeEndpoint),
            mBusEndpoints(std::move(mBusEndpoints)),
            sBusEndpoints(std::move(sBusEndpoints)),
            loopContinue(true) {}

CorvusSimWorker::~CorvusSimWorker() {
    std::cout << "[CorvusSimWorker] Destructor state: "
              << "name=" << (workerName.empty() ? "<unnamed>" : workerName)
                << ", loopCount=" << loopCount
                << ", lastStage=" << lastStage
                << ", prevTopSyncFlag=" << static_cast<unsigned int>(prevTopSyncFlag.getValue())
                << ", prevTopAllowSOutputFlag=" << static_cast<unsigned int>(prevTopAllowSOutputFlag.getValue())
                << ", simWorkerInputReadyFlag=" << static_cast<unsigned int>(simWorkerInputReadyFlag.getValue())
                << ", simWorkerSyncFlag=" << static_cast<unsigned int>(simWorkerSyncFlag.getValue()); 

    if (synctreeEndpoint) {
        std::cout << ", topSyncFlag(now)="
                  << static_cast<int>(synctreeEndpoint->getTopSyncFlag().getValue());
    } else {
        std::cout << ", synctreeEndpoint=null";
    }
    std::cout << std::endl;
}

void CorvusSimWorker::loop() {
    printf("SimWorker(%s) loop started\n", workerName.empty() ? "unnamed" : workerName.c_str());
    while(!hasStartFlagSeen()){}
    while(loopContinue) {
        loopCount++;
        logStage("waiting for top sync");
        while(loopContinue && !isTopSyncFlagRaised()) {}
        if (!loopContinue) break;
        logStage(std::string("get top sync flag as ") + std::to_string(prevTopSyncFlag.getValue()));
        loadMBusCInputs();
        loadSBusCInputs();
        logStage("raise sim worker input ready flag");
        raiseSimWorkerInputReadyFlag();
        logStage(std::string("sim worker input ready flag raised to ") + std::to_string(simWorkerInputReadyFlag.getValue()));
        cModule->eval();
        sendMBusCOutputs();
        copySInputs();
        sModule->eval();
        logStage("waiting for top allow S output");
        while(loopContinue && !isTopAllowSOutputFlagRaised()) {}
        if (!loopContinue) break;
        logStage(std::string("get top allow S output flag as ") + std::to_string(prevTopAllowSOutputFlag.getValue()));
        sendSBusSOutputs();
        logStage("raise sim worker sync flag");
        raiseSimWorkerSyncFlag();
        logStage(std::string("SimWorkerSync flag raised to ") + std::to_string(simWorkerSyncFlag.getValue()));
        copyLocalCInputs();
    }
}

void CorvusSimWorker::stop() {
    loopContinue = false;
}


void CorvusSimWorker::raiseSimWorkerInputReadyFlag() {
    simWorkerInputReadyFlag.updateToNext();
    synctreeEndpoint->setSimWorkerInputReadyFlag(simWorkerInputReadyFlag);
}

bool CorvusSimWorker::isTopAllowSOutputFlagRaised() {
    auto flag = synctreeEndpoint->getTopAllowSOutputFlag();
    if(flag.getValue() == prevTopAllowSOutputFlag.getValue()) {
        return false;
    } else if(flag.getValue() == prevTopAllowSOutputFlag.nextValue()) {
        prevTopAllowSOutputFlag.updateToNext();
        return true;
    } else {
        while(1){
            std::cerr << "[CorvusSimWorker] Fatal error: unexpected top allow S output flag value="
                    << static_cast<int>(flag.getValue())
                    << " (expected " << static_cast<int>(prevTopAllowSOutputFlag.getValue())
                    << " or " << static_cast<int>(prevTopAllowSOutputFlag.nextValue()) << ")"
                    << " in SimWorker(" << (workerName.empty() ? "unnamed" : workerName) << ")"
                    << std::endl;
        }
    }
}

void CorvusSimWorker::raiseSimWorkerSyncFlag() {
    simWorkerSyncFlag.updateToNext();
    synctreeEndpoint->setSimWorkerSyncFlag(simWorkerSyncFlag);
}

bool CorvusSimWorker::hasStartFlagSeen() {
    return synctreeEndpoint->getSimWorkerStartFlag().getValue() == CorvusSynctreeEndpoint::ValueFlag::START_GUARD;
}

bool CorvusSimWorker::isTopSyncFlagRaised() {
    if(synctreeEndpoint->getTopSyncFlag().getValue() == prevTopSyncFlag.getValue()) {
        return false;
    } else if (synctreeEndpoint->getTopSyncFlag().getValue() == prevTopSyncFlag.nextValue()) {
        prevTopSyncFlag.updateToNext();
        return true;
    } else {
        // Unexpected flag value, possibly due to reset
        // 严重错误，终止模拟
        while(1){
            std::cerr << "[CorvusSimWorker] Fatal error: unexpected top sync flag value="
                    << static_cast<int>(synctreeEndpoint->getTopSyncFlag().getValue())
                    << " (expected " << static_cast<int>(prevTopSyncFlag.getValue())
                    << " or " << static_cast<int>(prevTopSyncFlag.nextValue()) << ")"
                    << " in SimWorker(" << (workerName.empty() ? "unnamed" : workerName) << ")"
                    << std::endl;
        }
    }
}

void CorvusSimWorker::setName(std::string name) {
    workerName = std::move(name);
}

void CorvusSimWorker::logStage(std::string stageLabel) {
    lastStage = std::move(stageLabel);
    // printf("SimWorker(%s) loopCount=%llu stage=%s\n",
    //        workerName.empty() ? "unnamed" : workerName.c_str(),
    //        static_cast<unsigned long long>(loopCount),
    //        stageLabel.c_str());
}
