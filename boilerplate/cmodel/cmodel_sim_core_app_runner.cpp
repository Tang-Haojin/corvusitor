#include "cmodel_sim_core_app_runner.h"

#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

CModelSimCoreAppRunner::CModelSimCoreAppRunner(std::vector<CModelSimCoreApp*> simCoreApps)
    : simCoreApps(std::move(simCoreApps)) {}

CModelSimCoreAppRunner::~CModelSimCoreAppRunner() {
    stop();
}

void CModelSimCoreAppRunner::run() {
    for (auto* app : simCoreApps) {
        threads.emplace_back([app]() {
            if (app != nullptr) {
                app->loop();
            }
        });
    }
}

void CModelSimCoreAppRunner::stop() {
    for (auto& t : threads) {
#if defined(__unix__) || defined(__APPLE__)
        pthread_cancel(t.native_handle());
#endif
        if (t.joinable()) {
            t.join();
        }
    }
    threads.clear();
}
