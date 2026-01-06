#include "cmodel_sync_tree.h"

#include <stdexcept>

namespace {
SynctreeEndpoint::FlipFlag aggregateFlag(const std::vector<SynctreeEndpoint::FlipFlag>& flags) {
    if (flags.empty()) {
        return SynctreeEndpoint::FlipFlag::PENDING;
    }
    SynctreeEndpoint::FlipFlag first = flags.front();
    for (const auto& flag : flags) {
        if (flag != first) {
            return SynctreeEndpoint::FlipFlag::PENDING;
        }
    }
    return first;
}
} // namespace

CModelSyncTree::CModelSyncTree(uint32_t nSimCore)
    : masterSyncFlag(SynctreeEndpoint::FlipFlag::PENDING),
      simCoreCFinishFlag(nSimCore, SynctreeEndpoint::FlipFlag::PENDING),
      simCoreSFinishFlag(nSimCore, SynctreeEndpoint::FlipFlag::PENDING),
      masterEndpoint(nullptr) {
    masterEndpoint = new CModelMasterSynctreeEndpoint(this);
    simCoreEndpoints.reserve(nSimCore);
    for (uint32_t i = 0; i < nSimCore; ++i) {
        simCoreEndpoints.push_back(new CModelSimCoreSynctreeEndpoint(this, i));
    }
}

CModelSyncTree::~CModelSyncTree() {
    delete masterEndpoint;
    for (auto* endpoint : simCoreEndpoints) {
        delete endpoint;
    }
}

CModelMasterSynctreeEndpoint* CModelSyncTree::getMasterEndpoint() {
    return masterEndpoint;
}

CModelSimCoreSynctreeEndpoint* CModelSyncTree::getSimCoreEndpoint(uint32_t id) {
    if (id >= simCoreEndpoints.size()) {
        throw std::out_of_range("Invalid sim core endpoint id");
    }
    return simCoreEndpoints[id];
}

const std::vector<CModelSimCoreSynctreeEndpoint*>& CModelSyncTree::getSimCoreEndpoints() const {
    return simCoreEndpoints;
}

uint32_t CModelSyncTree::getSimCoreCount() const {
    return static_cast<uint32_t>(simCoreEndpoints.size());
}

CModelMasterSynctreeEndpoint::CModelMasterSynctreeEndpoint(CModelSyncTree* tree)
    : tree(tree) {}

void CModelMasterSynctreeEndpoint::forceSimCoreReset() {
}

bool CModelMasterSynctreeEndpoint::isMBusClear() {
    return true;
}

bool CModelMasterSynctreeEndpoint::isSBusClear() {
    return true;
}

SynctreeEndpoint::FlipFlag CModelMasterSynctreeEndpoint::getSimCoreCFinishFlag() {
    return aggregateFlag(tree->simCoreCFinishFlag);
}

SynctreeEndpoint::FlipFlag CModelMasterSynctreeEndpoint::getSimCoreSFinishFlag() {
    return aggregateFlag(tree->simCoreSFinishFlag);
}

void CModelMasterSynctreeEndpoint::setMasterSyncFlag(SynctreeEndpoint::FlipFlag flag) {
    tree->masterSyncFlag = flag;
}

CModelSimCoreSynctreeEndpoint::CModelSimCoreSynctreeEndpoint(CModelSyncTree* tree, uint32_t idx)
    : tree(tree), index(idx) {}

void CModelSimCoreSynctreeEndpoint::setCFinishFlag(SynctreeEndpoint::FlipFlag flag) {
    if (index >= tree->simCoreCFinishFlag.size()) {
        throw std::out_of_range("Invalid sim core index for C flag");
    }
    tree->simCoreCFinishFlag[index] = flag;
}

void CModelSimCoreSynctreeEndpoint::setSFinishFlag(SynctreeEndpoint::FlipFlag flag) {
    if (index >= tree->simCoreSFinishFlag.size()) {
        throw std::out_of_range("Invalid sim core index for S flag");
    }
    tree->simCoreSFinishFlag[index] = flag;
}

SynctreeEndpoint::FlipFlag CModelSimCoreSynctreeEndpoint::getMasterSyncFlag() {
    return tree->masterSyncFlag;
}
