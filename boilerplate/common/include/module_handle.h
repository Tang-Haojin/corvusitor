#ifndef MODULE_HANDLE_H
#define MODULE_HANDLE_H

class ModuleHandle {
public:
    virtual ~ModuleHandle() = default;
    virtual void eval() = 0;
};

#endif // MODULE_HANDLE_H