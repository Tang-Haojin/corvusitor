#ifndef CORVUS_CMODEL_IDEALIZED_BUS_H
#define CORVUS_CMODEL_IDEALIZED_BUS_H

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "corvus_bus_endpoint.h"

class CorvusCModelIdealizedBusEndpoint;

// Idealized bus that routes payloads between endpoints without timing behavior.
// All endpoints are created up front in the constructor. Payload delivery writes
// into target buffers with a lock; reads assume a single reader and are
// lock-free.
class CorvusCModelIdealizedBus {
public:
    explicit CorvusCModelIdealizedBus(uint32_t endpointCount);
    ~CorvusCModelIdealizedBus();
    CorvusCModelIdealizedBus(const CorvusCModelIdealizedBus&) = delete;
    CorvusCModelIdealizedBus& operator=(const CorvusCModelIdealizedBus&) = delete;

    std::shared_ptr<CorvusCModelIdealizedBusEndpoint> getEndpoint(uint32_t id);
    const std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>>& getEndpoints() const;
    uint32_t getEndpointCount() const;
    void deliver(uint32_t targetId, uint64_t payload);

private:
    std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>> endpoints;
};

class CorvusCModelIdealizedBusEndpoint : public CorvusBusEndpoint {
public:
    // Each endpoint owns a receive buffer; send routes via bus, recv pops or
    // returns 0 if empty. Buffer writes are locked; reads are assumed single
    // threaded and do not lock.
    CorvusCModelIdealizedBusEndpoint(CorvusCModelIdealizedBus* bus, uint32_t endpointId);
    ~CorvusCModelIdealizedBusEndpoint() override = default;
    CorvusCModelIdealizedBusEndpoint(const CorvusCModelIdealizedBusEndpoint&) = delete;
    CorvusCModelIdealizedBusEndpoint& operator=(const CorvusCModelIdealizedBusEndpoint&) = delete;

    void send(uint32_t targetId, uint64_t payload) override;
    uint64_t recv() override;
    int bufferCnt() const override;
    void clearBuffer() override;

private:
    friend class CorvusCModelIdealizedBus;
    void enqueue(uint64_t payload);

    CorvusCModelIdealizedBus* bus;
    uint32_t id;
    std::deque<uint64_t> buffer;
    std::mutex bufferMutex;
};

#endif // CORVUS_CMODEL_IDEALIZED_BUS_H
