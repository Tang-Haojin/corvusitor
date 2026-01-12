#include "corvus_sim_worker.h"
#include <cstdio>
#include <iostream>
#include <thread>
#include <utility>


CorvusSimWorker::CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
                                                                 std::vector<CorvusBusEndpoint*> mBusEndpoints,
                                                                 std::vector<CorvusBusEndpoint*> sBusEndpoints)
        : synctreeEndpoint(simCoreSynctreeEndpoint),
            mBusEndpoints(std::move(mBusEndpoints)),
            sBusEndpoints(std::move(sBusEndpoints)),
            loopContinue(true) {}

CorvusSimWorker::~CorvusSimWorker() {
    std::cout << "[CorvusSimWorker] Destructor state: "
              << "name=" << (workerName.empty() ? "<unnamed>" : workerName)
              << ", sFinishFlag=" << static_cast<int>(sFinishFlag.getValue())
              << ", prevMasterSyncFlag=" << static_cast<int>(prevMasterSyncFlag.getValue())
              << ", lastStage=" << lastStage 
              << ", loopCount=" << loopCount;
    if (synctreeEndpoint) {
        std::cout << ", masterSyncFlag(now)="
                  << static_cast<int>(synctreeEndpoint->getMasterSyncFlag().getValue());
    } else {
        std::cout << ", synctreeEndpoint=null";
    }
    std::cout << std::endl;
}

void CorvusSimWorker::loop() {
    printf("SimWorker(%s) loop started\n", workerName.empty() ? "unnamed" : workerName.c_str());
    while(loopContinue) {
        loopCount++;
        logStage("waiting for master sync");
        while(loopContinue && !isMasterSyncFlagRaised()) {
            // yield to allow stop requests to be observed promptly
            std::this_thread::yield();
        }
        if (!loopContinue) break;
        logStage(std::string("get master sync flag as ") + std::to_string(prevMasterSyncFlag.getValue()));
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
void CorvusSimWorker::raiseCFinishFlag() {
}

void CorvusSimWorker::raiseSFinishFlag() {
    sFinishFlag.updateToNext();
    synctreeEndpoint->setSFinishFlag(sFinishFlag);
}

bool CorvusSimWorker::hasStartFlagSeen() {
    return synctreeEndpoint->getSimWokerStartFlag().getValue() == CorvusSynctreeEndpoint::ValueFlag::START_GUARD;
}

bool CorvusSimWorker::isMasterSyncFlagRaised() {
    if(synctreeEndpoint->getMasterSyncFlag().getValue() == prevMasterSyncFlag.getValue()) {
        return false;
    } else if (synctreeEndpoint->getMasterSyncFlag().getValue() == prevMasterSyncFlag.nextValue()) {
        prevMasterSyncFlag.updateToNext();
        return true;
    } else {
        // Unexpected flag value, possibly due to reset
        // 严重错误，终止模拟
        std::cerr << "[CorvusSimWorker] Fatal error: unexpected master sync flag value="
                  << static_cast<int>(synctreeEndpoint->getMasterSyncFlag().getValue())
                  << " (expected " << static_cast<int>(prevMasterSyncFlag.getValue())
                  << " or " << static_cast<int>(prevMasterSyncFlag.nextValue()) << ")"
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
