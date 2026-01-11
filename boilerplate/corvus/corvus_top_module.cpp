#include "corvus_top_module.h"
#include <utility>
#include <iostream>

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
CorvusTopModule::CorvusTopModule(CorvusTopSynctreeEndpoint* masterSynctreeEndpoint,
                                 std::vector<CorvusBusEndpoint*> mBusEndpoints)
    : synctreeEndpoint(masterSynctreeEndpoint),
      mBusEndpoints(std::move(mBusEndpoints)) {
    setStage("constructed");
}

CorvusTopModule::~CorvusTopModule() {
    std::cout << "[CorvusTopModule] Destructor state: "
              << "masterSyncFlag=" << flagToString(masterSyncFlag)
              << ", prevCFinishFlag=" << flagToString(prevCFinishFlag)
              << ", prevSFinishFlag=" << flagToString(prevSFinishFlag)
              << ", lastStage=" << lastStage
              << std::endl;

    if (synctreeEndpoint) {
        std::cout << "[CorvusTopModule] Synctree flags at teardown: "
                  << "simCoreCFinish=" << flagToString(synctreeEndpoint->getSimCoreCFinishFlag())
                  << ", simCoreSFinish=" << flagToString(synctreeEndpoint->getSimCoreSFinishFlag())
                  << ", mBusClear=" << (synctreeEndpoint->isMBusClear() ? "true" : "false")
                  << ", sBusClear=" << (synctreeEndpoint->isSBusClear() ? "true" : "false")
                  << std::endl;
    } else {
        std::cout << "[CorvusTopModule] Synctree endpoint is null at teardown" << std::endl;
    }
}

void CorvusTopModule::resetSimWorker() {
    setStage("reset_sim_worker_start");
    synctreeEndpoint->forceSimCoreReset();
    setStage("reset_wait_s_finish");
    while(!synctreeEndpoint->isMBusClear() ||
          !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish() || !allSimCoreCFinish()) {}
    setStage("reset_s_finish_done");
    std::cout << "All simWorker S Finished" << std::endl;
    clearMBusRecvBuffer();
    setStage("reset_clear_mbus_buffer");
    prevSFinishFlag = CorvusSynctreeEndpoint::FlipFlag::PENDING;
    prevCFinishFlag = CorvusSynctreeEndpoint::FlipFlag::A_SIDE;
}

void CorvusTopModule::eval() {
    const uint64_t iter = evalCount++;
    setStage("eval_start");
    logStage("eval_start", iter);
    sendIAndEOutput();
    setStage("after_send_I_E_output");
    logStage("after_send_I_E_output", iter);
    setStage("wait_s_finish");
    while(!synctreeEndpoint->isMBusClear() || !synctreeEndpoint->isSBusClear() || !allSimCoreSFinish()) {}
    setStage("s_finish_done");
    logStage("s_finish_done", iter);
    raiseMasterSyncFlag();
    setStage("after_raise_master_sync");
    logStage("after_raise_master_sync", iter);
    setStage("wait_c_finish");
    while(!synctreeEndpoint->isMBusClear() || !allSimCoreCFinish()) {}
    setStage("c_finish_done");
    logStage("c_finish_done", iter);
    loadOAndEInput();
    setStage("after_load_O_E_input");
    logStage("after_load_O_E_input", iter);

}

void CorvusTopModule::evalE() {
    setStage("evalE_start");
    eHandle->eval();
    setStage("evalE_done");
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

void CorvusTopModule::setStage(const char* stage) {
    if (!stage) return;
    lastStage = stage;
}

void CorvusTopModule::logStage(const char* stageLabel, uint64_t iter) {
    if (!stageLabel) return;
    printf("TopModule iter=%llu stage=%s\n",
           static_cast<unsigned long long>(iter),
           stageLabel);
}
