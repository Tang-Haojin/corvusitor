#ifndef SYNCTREE_ENDPOINT_H
#define SYNCTREE_ENDPOINT_H

#include <cstdint>

class SynctreeEndpoint{
    public:
    enum class FlipFlag : uint8_t {
        PENDING = 0,
        A_SIDE = 1,
        B_SIDE = 2
    };
};

class MasterSynctreeEndpoint : public SynctreeEndpoint {
public:
  virtual ~MasterSynctreeEndpoint() = default;
  virtual void forceSimCoreReset();
  virtual bool isMBusClear() = 0;
  virtual bool isSBusClear() = 0;
  virtual FlipFlag getSimCoreCFinishFlag() = 0;
  virtual FlipFlag getSimCoreSFinishFlag() = 0;
  virtual void setMasterSyncFlag(FlipFlag flag) = 0;
};

class SimCoreSynctreeEndpoint : public SynctreeEndpoint {
public:
    virtual ~SimCoreSynctreeEndpoint() = default;
    virtual void setCFinishFlag(FlipFlag flag) = 0;
    virtual void setSFinishFlag(FlipFlag flag) = 0;
    virtual FlipFlag getMasterSyncFlag() = 0;
};
#endif