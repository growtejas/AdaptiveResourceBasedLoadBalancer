#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <algorithm>

struct TargetInfo {
    std::string name;
    std::string host;
    int port;
    std::atomic<double> cpu_percent{0.0};
    std::atomic<bool> healthy{true};

    TargetInfo() = default;

    TargetInfo(const std::string& n, const std::string& h, int p)
        : name(n), host(h), port(p) {}

    // Copy constructor
    TargetInfo(const TargetInfo& other)
        : name(other.name),
          host(other.host),
          port(other.port),
          cpu_percent(other.cpu_percent.load()),
          healthy(other.healthy.load()) {}

    // Move constructor
    TargetInfo(TargetInfo&& other) noexcept
        : name(std::move(other.name)),
          host(std::move(other.host)),
          port(other.port),
          cpu_percent(other.cpu_percent.load()),
          healthy(other.healthy.load()) {}

    // Copy assignment
    TargetInfo& operator=(const TargetInfo& other) {
        if (this != &other) {
            name = other.name;
            host = other.host;
            port = other.port;
            cpu_percent.store(other.cpu_percent.load());
            healthy.store(other.healthy.load());
        }
        return *this;
    }

    // Move assignment
    TargetInfo& operator=(TargetInfo&& other) noexcept {
        if (this != &other) {
            name = std::move(other.name);
            host = std::move(other.host);
            port = other.port;
            cpu_percent.store(other.cpu_percent.load());
            healthy.store(other.healthy.load());
        }
        return *this;
    }
};

class SharedState {
public:
    SharedState() = default;

    // Add initial targets (called before monitor starts)
    void add_target(const std::string& name, const std::string& host, int port);

    // Get a snapshot copy of targets (thread-safe)
    std::vector<TargetInfo> snapshot();

    // Update CPU% and health for target by name
    void update_target_stats(const std::string& name, double cpu_percent, bool healthy);

    // Select backend based on configured strategy
    std::optional<std::pair<std::string,int>> choose_backend();

    // Set balancing strategy: "least_cpu" or "round_robin"
    void set_strategy(const std::string& strategy);

private:
    std::mutex mtx_;
    std::vector<TargetInfo> targets_;
    std::string strategy_ = "least_cpu";
    size_t rr_index_ = 0;
};

