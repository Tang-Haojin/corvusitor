#ifndef CORVUS_CMODEL_SYNC_TREE_H
#define CORVUS_CMODEL_SYNC_TREE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

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
    static CorvusSynctreeEndpoint::ValueFlag loadFlag(const std::atomic<uint8_t>& flag);
    static void storeFlag(std::atomic<uint8_t>& dst, CorvusSynctreeEndpoint::ValueFlag flag);
    friend class CorvusCModelTopSynctreeEndpoint;
    friend class CorvusCModelSimWorkerSynctreeEndpoint;
    std::atomic<uint8_t> simWorkerStartFlag;
    std::atomic<uint8_t> topSyncFlag;
    std::atomic<uint8_t> topAllowSOutputFlag;
    std::vector<std::atomic<uint8_t>> simWorkerInputReadyFlag;
    std::vector<std::atomic<uint8_t>> simWorkerSyncFlag;
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
    ValueFlag getSimWorkerSyncFlag() override;
    ValueFlag getSimWorkerInputReadyFlag() override;
    void setTopAllowSOutputFlag(ValueFlag flag) override;
    void setTopSyncFlag(ValueFlag flag) override;
    void setSimWorkerStartFlag(ValueFlag flag) override;
private:
    CorvusCModelSyncTree* tree;
};

class CorvusCModelSimWorkerSynctreeEndpoint : public CorvusSimWorkerSynctreeEndpoint {
public:
    CorvusCModelSimWorkerSynctreeEndpoint(CorvusCModelSyncTree* tree, uint32_t idx);
    ~CorvusCModelSimWorkerSynctreeEndpoint() override = default;

    void setSimWorkerInputReadyFlag(ValueFlag flag) override;
    void setSimWorkerSyncFlag(ValueFlag flag) override;
    ValueFlag getTopSyncFlag() override;
    ValueFlag getSimWorkerStartFlag() override;
    ValueFlag getTopAllowSOutputFlag() override;

private:
    CorvusCModelSyncTree* tree;
    uint32_t index;
};

#endif // CORVUS_CMODEL_SYNC_TREE_H
