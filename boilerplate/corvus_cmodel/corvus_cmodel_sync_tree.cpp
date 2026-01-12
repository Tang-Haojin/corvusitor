#include "corvus_cmodel_sync_tree.h"

#include <memory>
#include <stdexcept>

namespace {
CorvusSynctreeEndpoint::ValueFlag aggregateFlag(const std::vector<CorvusSynctreeEndpoint::ValueFlag>& flags) {
    if (flags.empty()) {
        return CorvusSynctreeEndpoint::ValueFlag();
    }
    CorvusSynctreeEndpoint::ValueFlag first = flags.front();
    for (const auto& flag : flags) {
        if (flag.getValue() != first.getValue()) {
            return CorvusSynctreeEndpoint::ValueFlag();
        }
    }
    return first;
}
} // namespace

CorvusCModelSyncTree::CorvusCModelSyncTree(uint32_t nSimWorker)
        : topSyncFlag(CorvusSynctreeEndpoint::ValueFlag()),
            simWorkerSFinishFlag(nSimWorker, CorvusSynctreeEndpoint::ValueFlag()),
            topEndpoint(nullptr) {
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
    tree->topSyncFlag = flag;
}

void CorvusCModelTopSynctreeEndpoint::setSimWorkerStartFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    tree->simWorkerStartFlag = flag;
}

CorvusCModelSimWorkerSynctreeEndpoint::CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx)
    : tree(tree), index(idx) {}


void CorvusCModelSimWorkerSynctreeEndpoint::setSFinishFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    if (index >= tree->simWorkerSFinishFlag.size()) {
        throw std::out_of_range("Invalid sim worker index for S flag");
    }
    tree->simWorkerSFinishFlag[index] = flag;
}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSimWorkerSynctreeEndpoint::getTopSyncFlag() {
    return tree->topSyncFlag;
}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSimWorkerSynctreeEndpoint::getSimWorkerStartFlag() {
    return tree->simWorkerStartFlag;
}