#include "proxy_server.h"
#include <iostream>

using boost::asio::ip::tcp;

ProxyServer::ProxyServer(boost::asio::io_context& io_context, short listen_port, SharedState& state)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), listen_port)),
      state_(state) {}

void ProxyServer::start_accept() {
    auto client_socket = std::make_shared<tcp::socket>(io_context_);

    acceptor_.async_accept(*client_socket, [this, client_socket](const boost::system::error_code& ec) {
        if (!ec) {
            handle_accept(client_socket);
        } else {
            std::cerr << "[ERROR] Accept failed: " << ec.message() << "\n";
        }
        start_accept(); // continue accepting new clients
    });
}

void ProxyServer::handle_accept(std::shared_ptr<tcp::socket> client_socket) {
    auto backend_opt = state_.choose_backend();

    if (!backend_opt) {
        std::cerr << "[ERROR] No healthy backend available.\n";
        return;
    }

    auto [backend_host, backend_port] = *backend_opt;

    try {
        auto backend_socket = std::make_shared<tcp::socket>(io_context_);
        tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(backend_host, std::to_string(backend_port));
        boost::asio::connect(*backend_socket, endpoints);

        std::cout << "[INFO] Routing new connection → " 
                  << backend_host << ":" << backend_port << "\n";

        // Start bidirectional forwarding
        auto forward = [this](std::shared_ptr<tcp::socket> src, std::shared_ptr<tcp::socket> dst) {
            auto buffer = std::make_shared<std::vector<char>>(8192);
            src->async_read_some(boost::asio::buffer(*buffer),
                [buffer, src, dst, this](boost::system::error_code ec, std::size_t length) {
                    if (!ec) {
                        boost::asio::async_write(*dst, boost::asio::buffer(*buffer, length),
                            [src, dst, buffer](boost::system::error_code write_ec, std::size_t) {
                                if (!write_ec) {
                                    // Continue forwarding
                                    auto self = src;
                                    auto other = dst;
                                    auto next_buffer = std::make_shared<std::vector<char>>(8192);
                                    self->async_read_some(boost::asio::buffer(*next_buffer),
                                        [next_buffer, self, other](boost::system::error_code e, std::size_t len) {
                                            if (!e) {
                                                boost::asio::async_write(*other, boost::asio::buffer(*next_buffer, len),
                                                    [self, other, next_buffer](boost::system::error_code, std::size_t) {});
                                            }
                                        });
                                }
                            });
                    }
                });
        };

        // Start bidirectional relay
        forward(client_socket, backend_socket);
        forward(backend_socket, client_socket);

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to connect to backend (" 
                  << backend_host << ":" << backend_port << "): "
                  << e.what() << "\n";
    }
}

