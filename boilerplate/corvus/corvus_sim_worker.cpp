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
              << ", sFinishFlag=" << static_cast<int>(sFinishFlag.getValue())
              << ", prevTopSyncFlag=" << static_cast<int>(prevTopSyncFlag.getValue())
              << ", lastStage=" << lastStage 
              << ", loopCount=" << loopCount;
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
    while(loopContinue) {
        loopCount++;
        logStage("waiting for top sync");
        while(loopContinue && !isTopSyncFlagRaised()) {
            // yield to allow stop requests to be observed promptly
            std::this_thread::yield();
        }
        if (!loopContinue) break;
        logStage(std::string("get top sync flag as ") + std::to_string(prevTopSyncFlag.getValue()));
        loadRemoteCInputs();
        cModule->eval();
        sendRemoteCOutputs();
        loadSInputs();
        sModule->eval();
        sendRemoteSOutputs();
        raiseSFinishFlag();
        logStage(std::string("S finish flag raised to ") + std::to_string(sFinishFlag.getValue()));
        loadLocalCInputs();
    }
}

void CorvusSimWorker::stop() {
    loopContinue = false;
}

void CorvusSimWorker::raiseSFinishFlag() {
    sFinishFlag.updateToNext();
    synctreeEndpoint->setSFinishFlag(sFinishFlag);
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
        std::cerr << "[CorvusSimWorker] Fatal error: unexpected top sync flag value="
                  << static_cast<int>(synctreeEndpoint->getTopSyncFlag().getValue())
                  << " (expected " << static_cast<int>(prevTopSyncFlag.getValue())
                  << " or " << static_cast<int>(prevTopSyncFlag.nextValue()) << ")"
                  << " in SimWorker(" << (workerName.empty() ? "unnamed" : workerName) << ")"
                  << std::endl;
        stop();
        return false;
    }
}

void CorvusSimWorker::setName(std::string name) {
    workerName = std::move(name);
}

void CorvusSimWorker::logStage(std::string stageLabel) {
    lastStage = stageLabel;
    printf("SimWorker(%s) loopCount=%llu stage=%s\n",
           workerName.empty() ? "unnamed" : workerName.c_str(),
           static_cast<unsigned long long>(loopCount),
           stageLabel.c_str());
}
