#ifndef TOP_MODULE_H
#define TOP_MODULE_H

#include "top_ports.h"
#include "module_handle.h"

class TopModule {
public:
    virtual ~TopModule() = default;
    TopModule() = default;

    void init() {
        topPorts = createTopPorts();
        eHandle = createExternalModule();
    }
    void cleanup() {
        deleteExternalModule();
        deleteTopPorts();
    }

    TopPorts* topPorts = nullptr;
    virtual void prepareSimWorker() = 0;
    virtual void evalE() {
        eHandle->eval();
    }
    virtual void eval() = 0;
protected:
    virtual TopPorts* createTopPorts() = 0;
    virtual void deleteTopPorts() = 0;
    virtual ModuleHandle* createExternalModule() = 0;
    virtual void deleteExternalModule() = 0;
protected:
    ModuleHandle* eHandle = nullptr;
};

#endif
