#ifndef CORVUS_CMODEL_SYNC_TREE_H
#define CORVUS_CMODEL_SYNC_TREE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "corvus_synctree_endpoint.h"

class CorvusCModelTopSynctreeEndpoint;
class CorvusCModelSimWorkerSynctreeEndpoint;

// Coordinates sync flags between TopModule and SimWorkers.
class CorvusCModelSyncTree {
public:
    explicit CorvusCModelSyncTree(uint32_t nSimWorker);
    ~CorvusCModelSyncTree();
    CorvusCModelSyncTree(const CorvusCModelSyncTree&) = delete;
    CorvusCModelSyncTree& operator=(const CorvusCModelSyncTree&) = delete;

    std::shared_ptr<CorvusCModelTopSynctreeEndpoint> getTopEndpoint();
    std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint> getSimWorkerEndpoint(uint32_t id);
    const std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>>& getSimWorkerEndpoints() const;
    uint32_t getSimWorkerCount() const;

private:
    friend class CorvusCModelTopSynctreeEndpoint;
    friend class CorvusCModelSimWorkerSynctreeEndpoint;
    CorvusSynctreeEndpoint::ValueFlag simWorkerStartFlag;
    CorvusSynctreeEndpoint::ValueFlag topSyncFlag;
    std::vector<CorvusSynctreeEndpoint::ValueFlag> simWorkerSFinishFlag;
    std::shared_ptr<CorvusCModelTopSynctreeEndpoint> topEndpoint;
    std::vector<std::shared_ptr<CorvusCModelSimWorkerSynctreeEndpoint>> simWorkerEndpoints;
};

class CorvusCModelTopSynctreeEndpoint : public CorvusTopSynctreeEndpoint {
public:
    explicit CorvusCModelTopSynctreeEndpoint(CorvusCModelSyncTree* tree);
    ~CorvusCModelTopSynctreeEndpoint() override = default;

    void forceSimWorkerReset() override;
    bool isMBusClear() override;
    bool isSBusClear() override;
    ValueFlag getSimWorkerSFinishFlag() override;
    void setTopSyncFlag(ValueFlag flag) override;
    void setSimWorkerStartFlag(ValueFlag flag) override;
private:
    CorvusCModelSyncTree* tree;
};

class CorvusCModelSimWorkerSynctreeEndpoint : public CorvusSimWorkerSynctreeEndpoint {
public:
    CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx);
    ~CorvusCModelSimWorkerSynctreeEndpoint() override = default;

    void setSFinishFlag(ValueFlag flag) override;
    ValueFlag getTopSyncFlag() override;
    ValueFlag getSimWorkerStartFlag() override;

private:
    CorvusCModelSyncTree* tree;
    uint32_t index;
};

#endif // CORVUS_CMODEL_SYNC_TREE_H
