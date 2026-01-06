#ifndef CMODEL_SYNC_TREE_H
#define CMODEL_SYNC_TREE_H

#include <cstdint>
#include <vector>

#include "synctree_endpoint.h"

class CModelMasterSynctreeEndpoint;
class CModelSimCoreSynctreeEndpoint;

// Coordinates sync flags between master and sim cores.
class CModelSyncTree {
public:
    explicit CModelSyncTree(uint32_t nSimCore);
    ~CModelSyncTree();
    CModelSyncTree(const CModelSyncTree&) = delete;
    CModelSyncTree& operator=(const CModelSyncTree&) = delete;

    CModelMasterSynctreeEndpoint* getMasterEndpoint();
    CModelSimCoreSynctreeEndpoint* getSimCoreEndpoint(uint32_t id);
    const std::vector<CModelSimCoreSynctreeEndpoint*>& getSimCoreEndpoints() const;
    uint32_t getSimCoreCount() const;

private:
    friend class CModelMasterSynctreeEndpoint;
    friend class CModelSimCoreSynctreeEndpoint;
    SynctreeEndpoint::FlipFlag masterSyncFlag;
    std::vector<SynctreeEndpoint::FlipFlag> simCoreCFinishFlag;
    std::vector<SynctreeEndpoint::FlipFlag> simCoreSFinishFlag;
    CModelMasterSynctreeEndpoint* masterEndpoint;
    std::vector<CModelSimCoreSynctreeEndpoint*> simCoreEndpoints;
};

class CModelMasterSynctreeEndpoint : public MasterSynctreeEndpoint {
public:
    explicit CModelMasterSynctreeEndpoint(CModelSyncTree* tree);
    ~CModelMasterSynctreeEndpoint() override = default;

    void forceSimCoreReset() override;
    bool isMBusClear() override;
    bool isSBusClear() override;
    FlipFlag getSimCoreCFinishFlag() override;
    FlipFlag getSimCoreSFinishFlag() override;
    void setMasterSyncFlag(FlipFlag flag) override;

private:
    CModelSyncTree* tree;
};

class CModelSimCoreSynctreeEndpoint : public SimCoreSynctreeEndpoint {
public:
    CModelSimCoreSynctreeEndpoint(CModelSyncTree* tree, uint32_t idx);
    ~CModelSimCoreSynctreeEndpoint() override = default;

    void setCFinishFlag(FlipFlag flag) override;
    void setSFinishFlag(FlipFlag flag) override;
    FlipFlag getMasterSyncFlag() override;

private:
    CModelSyncTree* tree;
    uint32_t index;
};

#endif // CMODEL_SYNC_TREE_H
