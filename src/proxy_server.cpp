#include "proxy_server.h"
#include <iostream>
#include <optional>

using boost::asio::ip::tcp;

ProxyServer::ProxyServer(boost::asio::io_context& io_context, short listen_port, SharedState& state)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), listen_port)),
      state_(state) {}

void ProxyServer::start_accept() {
    auto client_socket = std::make_shared<tcp::socket>(io_context_);
    acceptor_.async_accept(*client_socket, [this, client_socket](boost::system::error_code ec) {
        if (!ec) handle_accept(client_socket);
        start_accept();
    });
}

std::pair<std::string,int> ProxyServer::get_backend_from_state() {
    auto opt = state_.choose_best_backend();
    if (opt) return *opt;
    // fallback: error host:port => will cause connection error and be logged
    return {"127.0.0.1", 8081};
}

void ProxyServer::handle_accept(std::shared_ptr<tcp::socket> client_socket) {
    auto backend = get_backend_from_state();
    std::string backend_host = backend.first;
    int backend_port = backend.second;

    auto backend_socket = std::make_shared<tcp::socket>(io_context_);
    tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(backend_host, std::to_string(backend_port));

    boost::asio::async_connect(*backend_socket, endpoints,
        [this, client_socket, backend_socket, backend_host, backend_port](boost::system::error_code ec, const tcp::endpoint&) {
            if (!ec) {
                std::cout << "[INFO] Connection routed to backend "
                          << backend_host << ":" << backend_port << std::endl;

                // safe recursive forwarding using shared_ptr of function
                auto forward_data = std::make_shared<std::function<void(std::shared_ptr<tcp::socket>, std::shared_ptr<tcp::socket>)>>();

                *forward_data = [forward_data](std::shared_ptr<tcp::socket> from, std::shared_ptr<tcp::socket> to) {
                    auto buffer = std::make_shared<std::array<char, 8192>>();
                    from->async_read_some(boost::asio::buffer(*buffer),
                        [forward_data, from, to, buffer](boost::system::error_code ec, std::size_t bytes_transferred) {
                            if (!ec) {
                                boost::asio::async_write(*to,
                                    boost::asio::buffer(buffer->data(), bytes_transferred),
                                    [forward_data, from, to, buffer](boost::system::error_code ec, std::size_t) {
                                        if (!ec) (*forward_data)(from, to);
                                    });
                            }
                        });
                };

                (*forward_data)(client_socket, backend_socket);
                (*forward_data)(backend_socket, client_socket);
            } else {
                std::cerr << "[ERROR] Failed to connect to backend (" << backend_host << ":" << backend_port << "): " << ec.message() << std::endl;
                // Mark target unhealthy in shared state? Already monitor handles that.
            }
        });
}

