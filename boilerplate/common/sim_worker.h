#ifndef SIM_WORKER_H
#define SIM_WORKER_H

#include "module_handle.h"

class SimWorker {
public:
    virtual ~SimWorker() = default;
    virtual void loop() = 0;

protected:
    ModuleHandle* cModule = nullptr;
    ModuleHandle* sModule = nullptr;
};

#endif
