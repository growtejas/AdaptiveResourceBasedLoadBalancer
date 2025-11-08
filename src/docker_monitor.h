#pragma once
#include "shared_state.h"
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>   // ✅ add this include

class DockerMonitor {
public:
    DockerMonitor(SharedState& state, int interval_seconds = 5);
    ~DockerMonitor();

    void start();
    void stop();

private:
    void run_loop();
    double compute_cpu_percent(const nlohmann::json& stats_json);

    SharedState& state_;
    int interval_seconds_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
