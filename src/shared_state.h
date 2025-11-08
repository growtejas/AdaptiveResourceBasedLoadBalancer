#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <optional>

struct TargetInfo {
    std::string name;
    std::string host;
    int port;
    std::atomic<double> cpu_percent{0.0};
    std::atomic<bool> healthy{true};

    TargetInfo() = default;

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

    // Assignment operator
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

    // add initial targets (called before monitor starts)
    void add_target(const std::string& name, const std::string& host, int port);

    // get a snapshot copy of targets (thread-safe)
    std::vector<TargetInfo> snapshot();

    // update CPU% and health for target by name
    void update_target_stats(const std::string& name, double cpu_percent, bool healthy);

    // choose best backend (lowest CPU%) among healthy targets
    // returns pair<host,port> if found, otherwise nullopt
    std::optional<std::pair<std::string,int>> choose_best_backend();

private:
    std::mutex mtx_;
    std::vector<TargetInfo> targets_;
};
