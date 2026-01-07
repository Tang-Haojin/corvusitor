#ifndef SIM_WORKER_H
#define SIM_WORKER_H

#include "module_handle.h"

class SimWorker {
public:
    virtual ~SimWorker() = default;
    SimWorker() = default;
    virtual void loop() = 0;
    void init() {
        createSimModules();
    }
    void cleanup() {
        deleteSimModules();
    }


protected:
    ModuleHandle* cModule = nullptr;
    ModuleHandle* sModule = nullptr;
    virtual void createSimModules() = 0;
    virtual void deleteSimModules() = 0;
};

#endif
