#ifndef TOP_MODULE_H
#define TOP_MODULE_H

#include "top_ports.h"
#include "module_handle.h"

class TopModule {
public:
    virtual ~TopModule() = default;
    TopModule() = default;

    void init() {
        createTopPorts();
        createExternalModule();
    }
    void cleanup() {
        deleteExternalModule();
    }

    TopPorts* topPorts = nullptr;
    virtual void resetSimWorker() = 0;
    virtual void evalE() {
        eHandle->eval();
    }
    virtual void eval() = 0;
protected:
    virtual void createTopPorts() = 0;
    virtual void createExternalModule() = 0;
    virtual void deleteExternalModule() = 0;
protected:
    ModuleHandle* eHandle = nullptr;
};

#endif
