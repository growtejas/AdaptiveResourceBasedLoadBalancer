#include "docker_monitor.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <chrono>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Helper: read command output
static std::string exec_cmd(const std::string& cmd) {
    std::array<char, 128> buffer{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

DockerMonitor::DockerMonitor(SharedState& state, int interval_seconds)
    : state_(state), interval_seconds_(interval_seconds) {}

DockerMonitor::~DockerMonitor() {
    stop();
}

void DockerMonitor::start() {
    running_.store(true);
    worker_ = std::thread(&DockerMonitor::run_loop, this);
}

void DockerMonitor::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
}

// Parse Docker stats JSON and compute CPU usage percentage
double DockerMonitor::compute_cpu_percent(const nlohmann::json& stats_json) {
    try {
        double cpu_delta = stats_json["cpu_stats"]["cpu_usage"]["total_usage"].get<double>()
                         - stats_json["precpu_stats"]["cpu_usage"]["total_usage"].get<double>();
        double system_delta = stats_json["cpu_stats"]["system_cpu_usage"].get<double>()
                            - stats_json["precpu_stats"]["system_cpu_usage"].get<double>();

        if (system_delta > 0.0 && cpu_delta > 0.0) {
            int cores = stats_json["cpu_stats"]["online_cpus"].get<int>();
            return (cpu_delta / system_delta) * cores * 100.0;
        }
    } catch (...) {
        // Ignore errors, fallback to 0
    }
    return 0.0;
}

// Background thread loop
void DockerMonitor::run_loop() {
    while (running_.load()) {
        try {
            auto snapshot = state_.snapshot();
            for (auto& t : snapshot) {
                std::ostringstream cmd;
                cmd << "docker stats " << t.name << " --no-stream --format '{{json .}}'";
                std::string output = exec_cmd(cmd.str());
                if (output.empty()) {
                    state_.update_target_stats(t.name, 0.0, false);
                    continue;
                }

                try {
                    // Docker --format '{{json .}}' returns multiple JSON lines
                    std::istringstream ss(output);
                    std::string line;
                    double avg_cpu = 0;
                    int count = 0;

                    while (std::getline(ss, line)) {
                        if (line.empty()) continue;
                        json stats_json = json::parse(line);
                        std::string cpu_str = stats_json["CPUPerc"];
                        cpu_str.erase(std::remove(cpu_str.begin(), cpu_str.end(), '%'), cpu_str.end());
                        avg_cpu += std::stod(cpu_str);
                        count++;
                    }

                    if (count > 0) avg_cpu /= count;
                    state_.update_target_stats(t.name, avg_cpu, true);
                    std::cout << "[Monitor] " << t.name << " CPU%=" << avg_cpu << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[Monitor ERROR] JSON parse failed for " << t.name
                              << ": " << e.what() << std::endl;
                    state_.update_target_stats(t.name, 0.0, false);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Monitor ERROR] " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds_));
    }
}

