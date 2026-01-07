#ifndef TOP_MODULE_H
#define TOP_MODULE_H

#include "top_ports.h"
#include "module_handle.h"

class TopModule {
public:
    virtual ~TopModule() = default;
    TopPorts* topPorts;
    virtual void init() = 0;
    virtual void evalE() {
        eHandle->eval();
    }
    virtual void eval() = 0;
protected:
    ModuleHandle* eHandle;
};

#endif