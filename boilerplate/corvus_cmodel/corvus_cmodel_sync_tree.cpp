#include "corvus_cmodel_sync_tree.h"

#include <memory>
#include <stdexcept>

namespace {
CorvusSynctreeEndpoint::ValueFlag aggregateFlag(const std::vector<std::atomic<uint8_t>>& flags) {
    if (flags.empty()) {
        return CorvusSynctreeEndpoint::ValueFlag();
    }
    uint8_t first = flags.front().load(std::memory_order_acquire);
    for (const auto& flag : flags) {
        if (flag.load(std::memory_order_acquire) != first) {
            return CorvusSynctreeEndpoint::ValueFlag();
        }
    }
    return CorvusSynctreeEndpoint::ValueFlag(first);
}
} // namespace

CorvusCModelSyncTree::CorvusCModelSyncTree(uint32_t nSimWorker)
        : simWorkerStartFlag(0),
          topSyncFlag(0),
          simWorkerSFinishFlag(nSimWorker),
          topEndpoint(nullptr) {
    simWorkerStartFlag.store(0, std::memory_order_relaxed);
    topSyncFlag.store(0, std::memory_order_relaxed);
    for (auto& f : simWorkerSFinishFlag) {
        f.store(0, std::memory_order_relaxed);
    }
    topEndpoint = std::make_shared<CorvusCModelTopSynctreeEndpoint>(this);
    simWorkerEndpoints.reserve(nSimWorker);
    for (uint32_t i = 0; i < nSimWorker; ++i) {
        simWorkerEndpoints.push_back(std::make_shared<CorvusCModelSimWorkerSynctreeEndpoint>(this, i));
    }
}

CorvusCModelSyncTree::~CorvusCModelSyncTree() {
    topEndpoint.reset();
    simWorkerEndpoints.clear();
}

std::shared_ptr<CorvusCModelTopSynctreeEndpoint> CorvusCModelSyncTree::getTopEndpoint() {
    return topEndpoint;
}

std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint> CorvusCModelSyncTree::getSimWorkerEndpoint(uint32_t id) {
    if (id >= simWorkerEndpoints.size()) {
        throw std::out_of_range("Invalid sim worker endpoint id");
    }
    return simWorkerEndpoints[id];
}

const std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>>& CorvusCModelSyncTree::getSimWorkerEndpoints() const {
    return simWorkerEndpoints;
}

uint32_t CorvusCModelSyncTree::getSimWorkerCount() const {
    return static_cast<uint32_t>(simWorkerEndpoints.size());
}

CorvusCModelTopSynctreeEndpoint::CorvusCModelTopSynctreeEndpoint(CorvusCModelSyncTree* tree)
    : tree(tree) {}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSyncTree::loadFlag(const std::atomic<uint8_t>& flag) {
    return CorvusSynctreeEndpoint::ValueFlag(flag.load(std::memory_order_acquire));
}

void CorvusCModelSyncTree::storeFlag(std::atomic<uint8_t>& dst, CorvusSynctreeEndpoint::ValueFlag flag) {
    dst.store(flag.getValue(), std::memory_order_release);
}

void CorvusCModelTopSynctreeEndpoint::forceSimWorkerReset() {
}

bool CorvusCModelTopSynctreeEndpoint::isMBusClear() {
    return true;
}

bool CorvusCModelTopSynctreeEndpoint::isSBusClear() {
    return true;
}


CorvusSynctreeEndpoint::ValueFlag CorvusCModelTopSynctreeEndpoint::getSimWorkerSFinishFlag() {
    return aggregateFlag(tree->simWorkerSFinishFlag);
}

void CorvusCModelTopSynctreeEndpoint::setTopSyncFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    CorvusCModelSyncTree::storeFlag(tree->topSyncFlag, flag);
}

void CorvusCModelTopSynctreeEndpoint::setSimWorkerStartFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    CorvusCModelSyncTree::storeFlag(tree->simWorkerStartFlag, flag);
}

CorvusCModelSimWorkerSynctreeEndpoint::CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx)
    : tree(tree), index(idx) {}


void CorvusCModelSimWorkerSynctreeEndpoint::setSFinishFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    if (index >= tree->simWorkerSFinishFlag.size()) {
        throw std::out_of_range("Invalid sim worker index for S flag");
    }
    CorvusCModelSyncTree::storeFlag(tree->simWorkerSFinishFlag[index], flag);
}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSimWorkerSynctreeEndpoint::getTopSyncFlag() {
    return CorvusCModelSyncTree::loadFlag(tree->topSyncFlag);
}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSimWorkerSynctreeEndpoint::getSimWorkerStartFlag() {
    return CorvusCModelSyncTree::loadFlag(tree->simWorkerStartFlag);
}
