#ifndef MODULE_HANDLE_H
#define MODULE_HANDLE_H

class ModuleHandle {
public:
    virtual ~ModuleHandle() = default;
    virtual void eval() = 0;
};

template <typename VModuleType>
class VerilatorModuleHandle : public ModuleHandle {
public:
    VModuleType* mp;
    VerilatorModuleHandle(VModuleType* modulePtr) : mp(modulePtr) {}
    void eval() override {
        mp->eval();
    }
};
#endif // MODULE_HANDLE_H