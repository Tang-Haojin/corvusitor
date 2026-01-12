#ifndef CORVUS_CMODEL_SYNC_TREE_H
#define CORVUS_CMODEL_SYNC_TREE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "corvus_synctree_endpoint.h"

class CorvusCModelMasterSynctreeEndpoint;
class CorvusCModelSimWorkerSynctreeEndpoint;

// Coordinates sync flags between master and sim cores.
class CorvusCModelSyncTree {
public:
    explicit CorvusCModelSyncTree(uint32_t nSimCore);
    ~CorvusCModelSyncTree();
    CorvusCModelSyncTree(const CorvusCModelSyncTree&) = delete;
    CorvusCModelSyncTree& operator=(const CorvusCModelSyncTree&) = delete;

    std::shared_ptr<CorvusCModelMasterSynctreeEndpoint> getMasterEndpoint();
    std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint> getSimCoreEndpoint(uint32_t id);
    const std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>>& getSimCoreEndpoints() const;
    uint32_t getSimCoreCount() const;

private:
    friend class CorvusCModelMasterSynctreeEndpoint;
    friend class CorvusCModelSimWorkerSynctreeEndpoint;
    CorvusSynctreeEndpoint::ValueFlag simWorkerStartFlag;
    CorvusSynctreeEndpoint::ValueFlag masterSyncFlag;
    std::vector<CorvusSynctreeEndpoint::ValueFlag> simCoreCFinishFlag;
    std::vector<CorvusSynctreeEndpoint::ValueFlag> simCoreSFinishFlag;
    std::shared_ptr<CorvusCModelMasterSynctreeEndpoint> masterEndpoint;
    std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>> simCoreEndpoints;
};

class CorvusCModelMasterSynctreeEndpoint : public CorvusTopSynctreeEndpoint {
public:
    explicit CorvusCModelMasterSynctreeEndpoint(CorvusCModelSyncTree* tree);
    ~CorvusCModelMasterSynctreeEndpoint() override = default;

    void forceSimCoreReset() override;
    bool isMBusClear() override;
    bool isSBusClear() override;
    ValueFlag getSimCoreSFinishFlag() override;
    void setMasterSyncFlag(ValueFlag flag) override;
    void setSimWorkerStartFlag(ValueFlag flag) override;
private:
    CorvusCModelSyncTree* tree;
};

class CorvusCModelSimWorkerSynctreeEndpoint : public CorvusSimWorkerSynctreeEndpoint {
public:
    CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx);
    ~CorvusCModelSimWorkerSynctreeEndpoint() override = default;

    void setSFinishFlag(ValueFlag flag) override;
    ValueFlag getMasterSyncFlag() override;
    ValueFlag getSimWokerStartFlag() override;

private:
    CorvusCModelSyncTree* tree;
    uint32_t index;
};

#endif // CORVUS_CMODEL_SYNC_TREE_H
