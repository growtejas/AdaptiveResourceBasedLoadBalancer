#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <functional>

#include "shared_state.h"

using boost::asio::ip::tcp;

class ProxyServer {
public:
    ProxyServer(boost::asio::io_context& io_context, short listen_port, SharedState& state);
    void start_accept();

private:
    void handle_accept(std::shared_ptr<tcp::socket> client_socket);
    std::pair<std::string,int> get_backend_from_state();

    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    SharedState& state_;
};

