#ifndef IDEALIZED_CMODEL_BUS_H
#define IDEALIZED_CMODEL_BUS_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "bus_endpoint.h"

class IdealizedCModelBusEndpoint;

// Idealized bus that routes payloads between endpoints without timing behavior.
// All endpoints are created up front in the constructor. Payload delivery writes
// into target buffers with a lock; reads assume a single reader and are
// lock-free.
class IdealizedCModelBus {
public:
    explicit IdealizedCModelBus(uint32_t endpointCount);
    ~IdealizedCModelBus();
    IdealizedCModelBus(const IdealizedCModelBus&) = delete;
    IdealizedCModelBus& operator=(const IdealizedCModelBus&) = delete;

    IdealizedCModelBusEndpoint* getEndpoint(uint32_t id);
    const std::vector<IdealizedCModelBusEndpoint*>& getEndpoints() const;
    uint32_t getEndpointCount() const;
    void deliver(uint32_t targetId, uint64_t payload);

private:
    std::vector<IdealizedCModelBusEndpoint*> endpoints;
};

class IdealizedCModelBusEndpoint : public BusEndpoint {
public:
    // Each endpoint owns a receive buffer; send routes via bus, recv pops or
    // returns 0 if empty. Buffer writes are locked; reads are assumed single
    // threaded and do not lock.
    IdealizedCModelBusEndpoint(IdealizedCModelBus* bus, uint32_t endpointId);
    ~IdealizedCModelBusEndpoint() override = default;
    IdealizedCModelBusEndpoint(const IdealizedCModelBusEndpoint&) = delete;
    IdealizedCModelBusEndpoint& operator=(const IdealizedCModelBusEndpoint&) = delete;

    void send(uint32_t targetId, uint64_t payload) override;
    uint64_t recv() override;
    int bufferCnt() const override;
    void clearBuffer() override;

private:
    friend class IdealizedCModelBus;
    void enqueue(uint64_t payload);

    IdealizedCModelBus* bus;
    uint32_t id;
    std::deque<uint64_t> buffer;
    std::mutex bufferMutex;
};

#endif // IDEALIZED_CMODEL_BUS_H
