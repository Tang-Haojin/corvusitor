#ifndef SYNCTREE_ENDPOINT_H
#define SYNCTREE_ENDPOINT_H

#include <cstdint>

class CorvusSynctreeEndpoint{
    public:
    enum class FlipFlag : uint8_t {
        PENDING = 0,
        A_SIDE = 1,
        B_SIDE = 2
    };
};

class CorvusTopSynctreeEndpoint : public CorvusSynctreeEndpoint {
public:
  virtual ~CorvusTopSynctreeEndpoint() = default;
  virtual void forceSimCoreReset();
  virtual bool isMBusClear() = 0;
  virtual bool isSBusClear() = 0;
  virtual FlipFlag getSimCoreCFinishFlag() = 0;
  virtual FlipFlag getSimCoreSFinishFlag() = 0;
  virtual void setMasterSyncFlag(FlipFlag flag) = 0;
};

class CorvusSimWorkerSynctreeEndpoint : public CorvusSynctreeEndpoint {
public:
    virtual ~CorvusSimWorkerSynctreeEndpoint() = default;
    virtual void setCFinishFlag(FlipFlag flag) = 0;
    virtual void setSFinishFlag(FlipFlag flag) = 0;
    virtual FlipFlag getMasterSyncFlag() = 0;
};
#endif