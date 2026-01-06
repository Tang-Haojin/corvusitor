#ifndef CMODEL_SIM_CORE_APP_RUNNER_H
#define CMODEL_SIM_CORE_APP_RUNNER_H

#include <thread>
#include <vector>

#include "cmodel_sim_core_app.h"

class CModelSimCoreAppRunner {
public:
    explicit CModelSimCoreAppRunner(std::vector<CModelSimCoreApp*> simCoreApps);
    ~CModelSimCoreAppRunner();

    void run();
    void stop();

private:
    std::vector<CModelSimCoreApp*> simCoreApps;
    std::vector<std::thread> threads;
};

#endif // CMODEL_SIM_CORE_APP_RUNNER_H
