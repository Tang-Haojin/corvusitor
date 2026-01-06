#ifndef VERILATOR_MODULE_HANDLE_H
#define VERILATOR_MODULE_HANDLE_H

#include "module_handle.h"
template <typename VModuleType>
class VerilatorModuleHandle : public ModuleHandle {
public:
    VModuleType* vModule;
    VerilatorModuleHandle(VModuleType* modulePtr) : vModule(module) {}
    void eval() override {
        vModule->eval();
    }
}
#endif