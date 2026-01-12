#include "corvus_sim_worker.h"
#include <cstdio>
#include <iostream>
#include <typeinfo>
#include <utility>


CorvusSimWorker::CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints,
                                 std::vector<CorvusBusEndpoint*> sBusEndpoints)
    : CorvusSimWorker(simCoreSynctreeEndpoint,
                      std::move(mBusEndpoints),
                      std::move(sBusEndpoints),
                      std::string{}) {}

CorvusSimWorker::CorvusSimWorker(CorvusSimWorkerSynctreeEndpoint* simCoreSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints,
                                 std::vector<CorvusBusEndpoint*> sBusEndpoints,
                                 std::string workerName)
    : synctreeEndpoint(simCoreSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)),
      sBusEndpoints(std::move(sBusEndpoints)) {
    setGeneratedName(std::move(workerName));
}

CorvusSimWorker::~CorvusSimWorker() {
    ensureWorkerName();
    std::cout << "[CorvusSimWorker] Destructor state: "
              << "name=" << (workerName.empty() ? "<unnamed>" : workerName)
              << ", sFinishFlag=" << sFinishFlag.getValue()
              << ", prevMasterSyncFlag=" << prevMasterSyncFlag.getValue()
              << ", lastStage=" << lastStage 
              << ", loopCount=" << loopCount;
    if (synctreeEndpoint) {
        std::cout << ", masterSyncFlag(now)="
                  << synctreeEndpoint->getMasterSyncFlag().getValue();
    } else {
        std::cout << ", synctreeEndpoint=null";
    }
    std::cout << std::endl;
}

void CorvusSimWorker::loop() {
    ensureWorkerName();
    printf("SimWorker(%s) loop started\n", workerName.empty() ? "unnamed" : workerName.c_str());
    while(1) {
        loopCount++;
        logStage("waiting for master sync");
        while(!isMasterSyncFlagRaised());
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

void CorvusSimWorker::raiseCFinishFlag() {
}

void CorvusSimWorker::raiseSFinishFlag() {
    sFinishFlag.updateToNext();
    synctreeEndpoint->setSFinishFlag(sFinishFlag);
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
        std::exit(1);
    }
}

void CorvusSimWorker::setName(std::string name) {
    workerName = std::move(name);
}


void CorvusSimWorker::setGeneratedName(std::string name) {
    if (!workerName.empty()) return;
    if (name.empty()) return;
    workerName = std::move(name);
}

void CorvusSimWorker::ensureWorkerName() {
    if (!workerName.empty()) return;
    workerName = typeid(*this).name();
}

void CorvusSimWorker::logStage(std::string stageLabel) {
    lastStage = stageLabel;
    printf("SimWorker(%s) loopCount=%llu stage=%s\n",
           workerName.empty() ? "unnamed" : workerName.c_str(),
           static_cast<unsigned long long>(loopCount),
           stageLabel.c_str());
}
