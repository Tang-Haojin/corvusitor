#include "idealized_cmodel_bus.h"

#include <stdexcept>

IdealizedCModelBus::IdealizedCModelBus(uint32_t endpointCount) {
    endpoints.reserve(endpointCount);
    for (uint32_t i = 0; i < endpointCount; ++i) {
        endpoints.push_back(new IdealizedCModelBusEndpoint(this, i));
    }
}

IdealizedCModelBus::~IdealizedCModelBus() {
    for (auto* endpoint : endpoints) {
        delete endpoint;
    }
}

IdealizedCModelBusEndpoint* IdealizedCModelBus::getEndpoint(uint32_t id) {
    if (id >= endpoints.size()) {
        throw std::out_of_range("Invalid endpoint id");
    }
    return endpoints[id];
}

const std::vector<IdealizedCModelBusEndpoint*>& IdealizedCModelBus::getEndpoints() const {
    return endpoints;
}

uint32_t IdealizedCModelBus::getEndpointCount() const {
    return static_cast<uint32_t>(endpoints.size());
}

void IdealizedCModelBus::deliver(uint32_t targetId, uint64_t payload) {
    IdealizedCModelBusEndpoint* target = getEndpoint(targetId);
    target->enqueue(payload);
}

IdealizedCModelBusEndpoint::IdealizedCModelBusEndpoint(IdealizedCModelBus* bus, uint32_t endpointId)
    : bus(bus), id(endpointId) {}

void IdealizedCModelBusEndpoint::send(uint32_t targetId, uint64_t payload) {
    if (bus == nullptr) {
        throw std::runtime_error("Bus is not available");
    }
    bus->deliver(targetId, payload);
}

uint64_t IdealizedCModelBusEndpoint::recv() {
    if (buffer.empty()) {
        return 0;
    }
    uint64_t payload = buffer.front();
    buffer.pop_front();
    return payload;
}

int IdealizedCModelBusEndpoint::bufferCnt() const {
    return static_cast<int>(buffer.size());
}

void IdealizedCModelBusEndpoint::clearBuffer() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    buffer.clear();
}

void IdealizedCModelBusEndpoint::enqueue(uint64_t payload) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    buffer.push_back(payload);
}
