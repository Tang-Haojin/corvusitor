#include "corvus_cmodel_idealized_bus.h"

#include <stdexcept>

CorvusCModelIdealizedBus::CorvusCModelIdealizedBus(uint32_t endpointCount) {
    endpoints.reserve(endpointCount);
    for (uint32_t i = 0; i < endpointCount; ++i) {
        endpoints.push_back(std::make_shared<CorvusCModelIdealizedBusEndpoint>(this, i));
    }
}

CorvusCModelIdealizedBus::~CorvusCModelIdealizedBus() {
    endpoints.clear();
}

std::shared_ptr<CorvusCModelIdealizedBusEndpoint> CorvusCModelIdealizedBus::getEndpoint(uint32_t id) {
    if (id >= endpoints.size()) {
        throw std::out_of_range("Invalid endpoint id");
    }
    return endpoints[id];
}

const std::vector<std::shared_ptr<CorvusCModelIdealizedBusEndpoint>>& CorvusCModelIdealizedBus::getEndpoints() const {
    return endpoints;
}

uint32_t CorvusCModelIdealizedBus::getEndpointCount() const {
    return static_cast<uint32_t>(endpoints.size());
}

void CorvusCModelIdealizedBus::deliver(uint32_t targetId, uint64_t payload) {
    std::shared_ptr<CorvusCModelIdealizedBusEndpoint> target = getEndpoint(targetId);
    target->enqueue(payload);
}

CorvusCModelIdealizedBusEndpoint::CorvusCModelIdealizedBusEndpoint(CorvusCModelIdealizedBus* bus, uint32_t endpointId)
    : bus(bus), id(endpointId) {}

void CorvusCModelIdealizedBusEndpoint::send(uint32_t targetId, uint64_t payload) {
    if (bus == nullptr) {
        throw std::runtime_error("Bus is not available");
    }
    bus->deliver(targetId, payload);
}

uint64_t CorvusCModelIdealizedBusEndpoint::recv() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (buffer.empty()) {
        // 报错
        throw std::out_of_range("read empty buffer!");
    }
    uint64_t payload = buffer.front();
    buffer.pop_front();
    return payload;
}

int CorvusCModelIdealizedBusEndpoint::bufferCnt() const {
    std::lock_guard<std::mutex> lock(bufferMutex);
    return static_cast<int>(buffer.size());
}

void CorvusCModelIdealizedBusEndpoint::clearBuffer() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    buffer.clear();
}

void CorvusCModelIdealizedBusEndpoint::enqueue(uint64_t payload) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    buffer.push_back(payload);
}
