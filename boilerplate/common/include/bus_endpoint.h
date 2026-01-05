#ifndef BUS_ENDPOINT_H
#define BUS_ENDPOINT_H

#include <cstdint>

// Virtual base class for bus endpoints
class BusEndpoint {
public:
  virtual ~BusEndpoint() = default;
  virtual void send(uint32_t targetId, uint64_t payload) = 0;
  virtual uint64_t recv() = 0;
  virtual int bufferCnt() const = 0;
  virtual void bufferClear() = 0;
};

#endif // BUS_ENDPOINT_H
