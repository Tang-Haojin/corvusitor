#include "corvus_sim_worker.h"
#include <cstdio>
#include <iostream>
#include <typeinfo>
#include <utility>

namespace {
const char* flagToString(CorvusSynctreeEndpoint::FlipFlag flag) {
    switch (flag) {
        case CorvusSynctreeEndpoint::FlipFlag::PENDING:
            return "PENDING";
        case CorvusSynctreeEndpoint::FlipFlag::A_SIDE:
            return "A_SIDE";
        case CorvusSynctreeEndpoint::FlipFlag::B_SIDE:
            return "B_SIDE";
    }
    return "UNKNOWN";
}
}  // namespace

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
              << ", cFinishFlag=" << flagToString(cFinishFlag)
              << ", sFinishFlag=" << flagToString(sFinishFlag)
              << ", prevMasterSyncFlag=" << flagToString(prevMasterSyncFlag)
              << ", lastStage=" << lastStage;
    if (synctreeEndpoint) {
        std::cout << ", masterSyncFlag(now)="
                  << flagToString(synctreeEndpoint->getMasterSyncFlag());
    } else {
        std::cout << ", synctreeEndpoint=null";
    }
    std::cout << std::endl;
}

void CorvusSimWorker::loop() {
    ensureWorkerName();
    printf("SimWorker(%s) loop started\n", workerName.empty() ? "unnamed" : workerName.c_str());
    while(1) {
        const uint64_t iter = loopCount++;
        loadRemoteCInputs();
        setStage("load_remote_c_inputs");
        cModule->eval();
        setStage("eval_c_module");
        //printf("SimWorker C module evaluated\n");
        sendRemoteCOutputs();
        setStage("send_remote_c_outputs");
        //printf("SimWorker sent remote C outputs\n");
        raiseCFinishFlag();
        setStage("raise_c_finish_flag");
        logStage("raise_c_finish_flag", iter);
        //printf("SimWorker raised C finish flag\n");
        loadSInputs();
        setStage("load_s_inputs");
        //printf("SimWorker loaded S inputs\n");
        sModule->eval();
        setStage("eval_s_module");
        //printf("SimWorker S module evaluated\n");
        sendRemoteSOutputs();
        setStage("send_remote_s_outputs");
        //printf("SimWorker sent remote S outputs\n");
        raiseSFinishFlag();
        setStage("raise_s_finish_flag");
        logStage("raise_s_finish_flag", iter);
        //printf("SimWorker raised S finish flag\n");
        loadLocalCInputs();
        setStage("load_local_c_inputs");
        //printf("SimWorker loaded local C inputs\n");
        setStage("wait_master_sync");
        while(!isMasterSyncFlagRaised());
        setStage("master_sync_seen");
        logStage("master_sync_seen", iter);
    }
}

void CorvusSimWorker::raiseCFinishFlag() {
    if(cFinishFlag == CorvusSynctreeEndpoint::FlipFlag::PENDING || cFinishFlag == CorvusSynctreeEndpoint::FlipFlag::B_SIDE) {
        cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        cFinishFlag = CorvusSynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setCFinishFlag(cFinishFlag);
}

void CorvusSimWorker::raiseSFinishFlag() {
    if(sFinishFlag == CorvusSynctreeEndpoint::FlipFlag::PENDING || sFinishFlag == CorvusSynctreeEndpoint::FlipFlag::B_SIDE) {
        sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::A_SIDE;
    } else {
        sFinishFlag = CorvusSynctreeEndpoint::FlipFlag::B_SIDE;
    }
    synctreeEndpoint->setSFinishFlag(sFinishFlag);
}

bool CorvusSimWorker::isMasterSyncFlagRaised() {
    if(synctreeEndpoint->getMasterSyncFlag() == CorvusSynctreeEndpoint::FlipFlag::PENDING) {
        return false;
    }   
    if(prevMasterSyncFlag == synctreeEndpoint->getMasterSyncFlag()) {
        return false;
    } else {
        prevMasterSyncFlag = synctreeEndpoint->getMasterSyncFlag();
        return true;
    }
}

void CorvusSimWorker::setName(std::string name) {
    workerName = std::move(name);
}

void CorvusSimWorker::setStage(const char* stage) {
    if (!stage) return;
    lastStage = stage;
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

void CorvusSimWorker::logStage(const char* stageLabel, uint64_t iter) {
    if (!stageLabel) return;
    printf("SimWorker(%s) iter=%llu stage=%s\n",
           workerName.empty() ? "unnamed" : workerName.c_str(),
           static_cast<unsigned long long>(iter),
           stageLabel);
}
