#ifndef CORVUS_BUS_ENDPOINT_H
#define CORVUS_BUS_ENDPOINT_H

#include <cstdint>

// Virtual base class for bus endpoints
class CorvusBusEndpoint {
public:
  virtual ~CorvusBusEndpoint() = default;
  virtual void send(uint32_t targetId, uint64_t payload) = 0;
  virtual uint64_t recv() = 0;
  virtual int bufferCnt() const = 0;
  virtual void clearBuffer() = 0;
};

#endif // CORVUS_BUS_ENDPOINT_H