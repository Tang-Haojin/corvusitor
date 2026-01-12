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

CorvusCModelSyncTree::CorvusCModelSyncTree(uint32_t nSimCore)
    : masterSyncFlag(CorvusSynctreeEndpoint::ValueFlag()),
      simCoreSFinishFlag(nSimCore, CorvusSynctreeEndpoint::ValueFlag()),
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


CorvusSynctreeEndpoint::ValueFlag CorvusCModelMasterSynctreeEndpoint::getSimCoreSFinishFlag() {
    return aggregateFlag(tree->simCoreSFinishFlag);
}

void CorvusCModelMasterSynctreeEndpoint::setMasterSyncFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    tree->masterSyncFlag = flag;
}

void CorvusCModelMasterSynctreeEndpoint::setSimWorkerStartFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    tree->simWorkerStartFlag = flag;
}

CorvusCModelSimWorkerSynctreeEndpoint::CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx)
    : tree(tree), index(idx) {}


void CorvusCModelSimWorkerSynctreeEndpoint::setSFinishFlag(CorvusSynctreeEndpoint::ValueFlag flag) {
    if (index >= tree->simCoreSFinishFlag.size()) {
        throw std::out_of_range("Invalid sim core index for S flag");
    }
    tree->simCoreSFinishFlag[index] = flag;
}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSimWorkerSynctreeEndpoint::getMasterSyncFlag() {
    return tree->masterSyncFlag;
}

CorvusSynctreeEndpoint::ValueFlag CorvusCModelSimWorkerSynctreeEndpoint::getSimWokerStartFlag() {
    return tree->simWorkerStartFlag;
}