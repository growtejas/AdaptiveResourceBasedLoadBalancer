#include "docker_monitor.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

// helper for curl response
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), total);
    return total;
}

DockerMonitor::DockerMonitor(SharedState& state, int interval_seconds)
    : state_(state), interval_seconds_(interval_seconds) {}

DockerMonitor::~DockerMonitor() {
    stop();
}

void DockerMonitor::start() {
    if (running_.load()) return;
    running_.store(true);
    worker_ = std::thread(&DockerMonitor::run_loop, this);
}

void DockerMonitor::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (worker_.joinable()) worker_.join();
}

// Compute CPU % using Docker stats JSON. Returns -1 on error.
double DockerMonitor::compute_cpu_percent(const nlohmann::json& s) {
    try {
        // docker stats JSON has fields: cpu_stats, precpu_stats
        if (!s.contains("cpu_stats") || !s.contains("precpu_stats")) return -1.0;

        const auto& cpu_stats = s["cpu_stats"];
        const auto& precpu_stats = s["precpu_stats"];

        // total_usage
        uint64_t total_usage = 0;
        uint64_t pre_total_usage = 0;
        if (cpu_stats.contains("cpu_usage") && cpu_stats["cpu_usage"].contains("total_usage"))
            total_usage = cpu_stats["cpu_usage"]["total_usage"].get<uint64_t>();
        if (precpu_stats.contains("cpu_usage") && precpu_stats["cpu_usage"].contains("total_usage"))
            pre_total_usage = precpu_stats["cpu_usage"]["total_usage"].get<uint64_t>();

        // system_cpu_usage
        uint64_t system_cpu = 0;
        uint64_t pre_system_cpu = 0;
        if (cpu_stats.contains("system_cpu_usage"))
            system_cpu = cpu_stats["system_cpu_usage"].get<uint64_t>();
        if (precpu_stats.contains("system_cpu_usage"))
            pre_system_cpu = precpu_stats["system_cpu_usage"].get<uint64_t>();

        uint64_t cpu_delta = (system_cpu > pre_system_cpu) ? (system_cpu - pre_system_cpu) : 0;
        uint64_t total_delta = (total_usage > pre_total_usage) ? (total_usage - pre_total_usage) : 0;

        // number of online CPUs
        unsigned int online_cpus = 0;
        if (cpu_stats.contains("online_cpus")) {
            online_cpus = cpu_stats["online_cpus"].get<unsigned int>();
        } else if (cpu_stats.contains("cpu_usage") && cpu_stats["cpu_usage"].contains("percpu_usage")) {
            online_cpus = cpu_stats["cpu_usage"]["percpu_usage"].size();
        } else {
            online_cpus = 1;
        }

        if (cpu_delta == 0 || total_delta == 0) return 0.0;

        double cpu_percent = (double)total_delta / (double)cpu_delta * (double)online_cpus * 100.0;
        return cpu_percent;
    } catch (...) {
        return -1.0;
    }
}

void DockerMonitor::run_loop() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Monitor] libcurl init failed\n";
        return;
    }

    // Talk to Docker over unix socket
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    while (running_.load()) {
        // snapshot targets
        auto targets = state_.snapshot();
        for (const auto& t : targets) {
            std::string response;
            std::string url = "http://localhost/containers/" + t.name + "/stats?stream=false";

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L); // 3 sec per container

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "[Monitor] curl error for " << t.name << ": " << curl_easy_strerror(res) << "\n";
                state_.update_target_stats(t.name, 9999.0, false); // mark unhealthy
                response.clear();
                continue;
            }

            try {
                auto j = nlohmann::json::parse(response);
                double cpu = compute_cpu_percent(j);
                bool healthy = (cpu >= 0.0); // cpu -1 means parse error
                if (!healthy) {
                    state_.update_target_stats(t.name, 9999.0, false);
                    std::cerr << "[Monitor] parse error for " << t.name << "\n";
                } else {
                    state_.update_target_stats(t.name, cpu, true);
                    // debug print
                    std::cout << "[Monitor] " << t.name << " CPU%=" << cpu << "\n";
                }
            } catch (const std::exception& ex) {
                std::cerr << "[Monitor] JSON parse error for " << t.name << ": " << ex.what() << "\n";
                state_.update_target_stats(t.name, 9999.0, false);
            }
        }

        // sleep for interval (use small sleeps to allow stop() to be responsive)
        for (int i = 0; i < interval_seconds_ * 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    curl_easy_cleanup(curl);
}

