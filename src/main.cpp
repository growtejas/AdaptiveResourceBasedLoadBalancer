#include "proxy_server.h"
#include "shared_state.h"
#include "docker_monitor.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <boost/asio.hpp>

std::atomic<bool> stop_flag{false};
void handle_sigint(int) { stop_flag.store(true); }

int main() {
    signal(SIGINT, handle_sigint);

    try {
        // Load YAML config
        YAML::Node config = YAML::LoadFile("config.yaml");
        int listen_port = config["listen_port"].as<int>();
        int monitor_interval = config["monitor_interval_seconds"].as<int>();
        std::string strategy = config["strategy"] ? config["strategy"].as<std::string>() : "least_cpu";

        SharedState state;
        state.set_strategy(strategy);  // ✅ tell SharedState which mode to use

        for (const auto& node : config["targets"]) {
            std::string name = node["name"].as<std::string>();
            int port = node["host_port"].as<int>();
            state.add_target(name, "127.0.0.1", port);
            std::cout << "[Config] Added " << name << " on 127.0.0.1:" << port << "\n";
        }

        std::cout << "[INFO] Load-balancing strategy: " << strategy << "\n";

        DockerMonitor monitor(state, monitor_interval);
        monitor.start();

        boost::asio::io_context io_context;
        ProxyServer server(io_context, listen_port, state);
        std::cout << "[INFO] Listening on port " << listen_port << "\n";

        server.start_accept();

        // CLI loop
        std::thread cli_thread([&]() {
            std::string cmd;
            while (!stop_flag.load()) {
                std::cout << "> ";
                if (!std::getline(std::cin, cmd)) break;

                if (cmd == "exit") {
                    stop_flag.store(true);
                    break;
                }

                if (cmd == "status") {
                    auto snap = state.snapshot();
                    std::cout << "NAME\tPORT\tHEALTH\tCPU%\n";
                    for (auto& t : snap)
                        std::cout << t.name << "\t" << t.port << "\t"
                                  << (t.healthy ? "OK" : "DOWN") << "\t"
                                  << t.cpu_percent.load() << "\n";
                }
            }
        });

        // main event loop
        while (!stop_flag.load()) {
            io_context.run_for(std::chrono::milliseconds(200));
        }

        monitor.stop();
        cli_thread.join();
        std::cout << "[INFO] Graceful shutdown complete.\n";
    } catch (const std::exception& ex) {
        std::cerr << "[FATAL] " << ex.what() << std::endl;
        return 1;
    }
}

