#include "shared_state.h"
#include <algorithm>

void SharedState::add_target(const std::string& name, const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(mtx_);
    TargetInfo t;
    t.name = name;
    t.host = host;
    t.port = port;
    t.cpu_percent = 0.0;
    t.healthy = true;
    targets_.push_back(std::move(t));
}

std::vector<TargetInfo> SharedState::snapshot() {
    std::lock_guard<std::mutex> lock(mtx_);
    return targets_;
}

void SharedState::update_target_stats(const std::string& name, double cpu_percent, bool healthy) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto &t : targets_) {
        if (t.name == name) {
            t.cpu_percent.store(cpu_percent);
            t.healthy.store(healthy);
            return;
        }
    }
    // if not found, ignore (or optionally add)
}

std::optional<std::pair<std::string,int>> SharedState::choose_best_backend() {
    std::lock_guard<std::mutex> lock(mtx_);
    bool found = false;
    double best_cpu = std::numeric_limits<double>::infinity();
    std::pair<std::string,int> chosen;
    for (auto &t : targets_) {
        if (!t.healthy.load()) continue;
        double c = t.cpu_percent.load();
        if (!found || c < best_cpu) {
            best_cpu = c;
            chosen = {t.host, t.port};
            found = true;
        }
    }
    if (found) return chosen;
    return std::nullopt;
}

