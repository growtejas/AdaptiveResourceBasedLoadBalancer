#include "shared_state.h"
#include <algorithm>
#include <limits>

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
    // not found → ignore
}

void SharedState::set_strategy(const std::string& strategy) {
    std::lock_guard<std::mutex> lock(mtx_);
    strategy_ = strategy;
}

std::optional<std::pair<std::string,int>> SharedState::choose_backend() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (targets_.empty()) return std::nullopt;

    // keep only healthy
    std::vector<TargetInfo*> healthy_targets;
    for (auto& t : targets_) {
        if (t.healthy.load()) healthy_targets.push_back(&t);
    }
    if (healthy_targets.empty()) return std::nullopt;

    // ----- ROUND ROBIN -----
    if (strategy_ == "round_robin") {
        TargetInfo* target = healthy_targets[rr_index_ % healthy_targets.size()];
        rr_index_ = (rr_index_ + 1) % healthy_targets.size();
        return std::make_pair(target->host, target->port);
    }

    // ----- LEAST CPU -----
    TargetInfo* best = nullptr;
    double best_cpu = std::numeric_limits<double>::infinity();
    for (auto* t : healthy_targets) {
        double c = t->cpu_percent.load();
        if (!best || c < best_cpu) {
            best = t;
            best_cpu = c;
        }
    }
    if (best) return std::make_pair(best->host, best->port);
    return std::nullopt;
}

