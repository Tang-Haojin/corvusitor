#ifndef SYNCTREE_ENDPOINT_H
#define SYNCTREE_ENDPOINT_H

#include <cstdint>

class CorvusSynctreeEndpoint{
    public:
    class ValueFlag {
    public:
        uint8_t getValue() const {
            return value;
        }
        uint8_t nextValue() const {
            uint8_t v = (value + 1) & MASK;
            return v == 0 ? 1 : v;
        }
        void updateToNext() {
            value = nextValue();
        }
        void reset() {
            value = 0;
        }
    private:
        uint8_t value = 0;
        constexpr static uint8_t MASK = 0xFF;
    };
};

class CorvusTopSynctreeEndpoint : public CorvusSynctreeEndpoint {
public:
  virtual ~CorvusTopSynctreeEndpoint() = default;
  virtual void forceSimCoreReset();
  virtual bool isMBusClear() = 0;
  virtual bool isSBusClear() = 0;
  virtual ValueFlag getSimCoreSFinishFlag() = 0;
  virtual void setMasterSyncFlag(ValueFlag flag) = 0;
};

class CorvusSimWorkerSynctreeEndpoint : public CorvusSynctreeEndpoint {
public:
    virtual ~CorvusSimWorkerSynctreeEndpoint() = default;
    virtual void setSFinishFlag(ValueFlag flag) = 0;
    virtual ValueFlag getMasterSyncFlag() = 0;
};
#endif