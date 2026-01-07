#include "corvus_cmodel_sim_worker_runner.h"

#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

CorvusCModelSimWorkerRunner::CorvusCModelSimWorkerRunner(std::vector<std::shared_ptr<CorvusSimWorker>> simWorkers)
    : simWorkers(std::move(simWorkers)) {}

CorvusCModelSimWorkerRunner::~CorvusCModelSimWorkerRunner() {
    stop();
}

void CorvusCModelSimWorkerRunner::run() {
    for (auto worker : simWorkers) {
        threads.emplace_back([worker]() {
            if (worker) {
                worker->loop();
            }
        });
    }
}

void CorvusCModelSimWorkerRunner::stop() {
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
