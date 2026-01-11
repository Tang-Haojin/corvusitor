#include "corvus_cmodel_sync_tree.h"

#include <memory>
#include <stdexcept>

namespace {
CorvusSynctreeEndpoint::FlipFlag aggregateFlag(const std::vector<CorvusSynctreeEndpoint::FlipFlag>& flags) {
    if (flags.empty()) {
        return CorvusSynctreeEndpoint::FlipFlag::PENDING;
    }
    CorvusSynctreeEndpoint::FlipFlag first = flags.front();
    for (const auto& flag : flags) {
        if (flag != first) {
            return CorvusSynctreeEndpoint::FlipFlag::PENDING;
        }
    }
    return first;
}
} // namespace

CorvusCModelSyncTree::CorvusCModelSyncTree(uint32_t nSimCore)
    : masterSyncFlag(CorvusSynctreeEndpoint::FlipFlag::PENDING),
      simCoreCFinishFlag(nSimCore, CorvusSynctreeEndpoint::FlipFlag::PENDING),
      simCoreSFinishFlag(nSimCore, CorvusSynctreeEndpoint::FlipFlag::PENDING),
      masterEndpoint(nullptr) {
    masterEndpoint = std::make_shared<CorvusCModelMasterSynctreeEndpoint>(this);
    simCoreEndpoints.reserve(nSimCore);
    for (uint32_t i = 0; i < nSimCore; ++i) {
        simCoreEndpoints.push_back(std::make_shared<CorvusCModelSimWorkerSynctreeEndpoint>(this, i));
    }
}

CorvusCModelSyncTree::~CorvusCModelSyncTree() {
    masterEndpoint.reset();
    simCoreEndpoints.clear();
}

std::shared_ptr<CorvusCModelMasterSynctreeEndpoint> CorvusCModelSyncTree::getMasterEndpoint() {
    return masterEndpoint;
}

std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint> CorvusCModelSyncTree::getSimCoreEndpoint(uint32_t id) {
    if (id >= simCoreEndpoints.size()) {
        throw std::out_of_range("Invalid sim core endpoint id");
    }
    return simCoreEndpoints[id];
}

const std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>>& CorvusCModelSyncTree::getSimCoreEndpoints() const {
    return simCoreEndpoints;
}

uint32_t CorvusCModelSyncTree::getSimCoreCount() const {
    return static_cast<uint32_t>(simCoreEndpoints.size());
}

CorvusCModelMasterSynctreeEndpoint::CorvusCModelMasterSynctreeEndpoint(CorvusCModelSyncTree* tree)
    : tree(tree) {}

void CorvusCModelMasterSynctreeEndpoint::forceSimCoreReset() {
}

bool CorvusCModelMasterSynctreeEndpoint::isMBusClear() {
    return true;
}

bool CorvusCModelMasterSynctreeEndpoint::isSBusClear() {
    return true;
}

CorvusSynctreeEndpoint::FlipFlag CorvusCModelMasterSynctreeEndpoint::getSimCoreCFinishFlag() {
    std::lock_guard<std::mutex> lock(tree->mu);
    return aggregateFlag(tree->simCoreCFinishFlag);
}

CorvusSynctreeEndpoint::FlipFlag CorvusCModelMasterSynctreeEndpoint::getSimCoreSFinishFlag() {
    std::lock_guard<std::mutex> lock(tree->mu);
    return aggregateFlag(tree->simCoreSFinishFlag);
}

void CorvusCModelMasterSynctreeEndpoint::setMasterSyncFlag(CorvusSynctreeEndpoint::FlipFlag flag) {
    std::lock_guard<std::mutex> lock(tree->mu);
    tree->masterSyncFlag = flag;
}

CorvusCModelSimWorkerSynctreeEndpoint::CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx)
    : tree(tree), index(idx) {}

void CorvusCModelSimWorkerSynctreeEndpoint::setCFinishFlag(CorvusSynctreeEndpoint::FlipFlag flag) {
    if (index >= tree->simCoreCFinishFlag.size()) {
        throw std::out_of_range("Invalid sim core index for C flag");
    }
    std::lock_guard<std::mutex> lock(tree->mu);
    tree->simCoreCFinishFlag[index] = flag;
}

void CorvusCModelSimWorkerSynctreeEndpoint::setSFinishFlag(CorvusSynctreeEndpoint::FlipFlag flag) {
    if (index >= tree->simCoreSFinishFlag.size()) {
        throw std::out_of_range("Invalid sim core index for S flag");
    }
    std::lock_guard<std::mutex> lock(tree->mu);
    tree->simCoreSFinishFlag[index] = flag;
}

CorvusSynctreeEndpoint::FlipFlag CorvusCModelSimWorkerSynctreeEndpoint::getMasterSyncFlag() {
    std::lock_guard<std::mutex> lock(tree->mu);
    return tree->masterSyncFlag;
}
