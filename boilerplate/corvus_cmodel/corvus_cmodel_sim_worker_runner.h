#ifndef CORVUS_CMODEL_SIM_WORKER_RUNNER_H
#define CORVUS_CMODEL_SIM_WORKER_RUNNER_H

#include <memory>
#include <thread>
#include <vector>

#include "../corvus/corvus_sim_worker.h"

class CorvusCModelSimWorkerRunner {
public:
    explicit CorvusCModelSimWorkerRunner(std::vector<std::shared_ptr<CorvusSimWorker>> simWorkers);
    ~CorvusCModelSimWorkerRunner();

    void run();
    void stop();

private:
    std::vector<std::shared_ptr<CorvusSimWorker>> simWorkers;
    std::vector<std::thread> threads;
};

#endif // CORVUS_CMODEL_SIM_WORKER_RUNNER_H
