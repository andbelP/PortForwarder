#include "Agent.hpp"
#include <iostream>

void Agent::Start() {
    ConnectControl();
    SendPublicPort();
    ReadTrigger();
    io_.run();
}

void Agent::ConnectControl() {
    control_socket_.connect(cfg_.server_control);
}

void Agent::SendPublicPort() {
    uint16_t port = cfg_.public_port;
    boost::asio::write(control_socket_, boost::asio::buffer(&port, sizeof(port)));
    // CHECK FOR ENDIANNESS HERE
}

void Agent::ReadTrigger() {
    auto self = shared_from_this(); // MAYBE I DONT NEED 'self' because we use blocking start()
    control_socket_.async_read_some(
        boost::asio::buffer(trigger_buf_),
        [this, self](const boost::system::error_code& ec, size_t bytes_read) {
            if (!ec) {
                OpenTunnel();
                ReadTrigger();
            }
            else{
                std::cerr << "Error reading trigger: " << ec.message() << std::endl;
            }
        }
    );
}

void Agent::OpenTunnel() {
    tcp::socket app_socket(io_);
    app_socket.connect(cfg_.app);

    tcp::socket proxy_socket(io_);
    proxy_socket.connect(cfg_.server_proxy);

    auto tunnel = Tunnel::Create(std::move(app_socket), std::move(proxy_socket));
    tunnel->Start();
}