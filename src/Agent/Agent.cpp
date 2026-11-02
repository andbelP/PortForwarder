#include "Agent.hpp"
#include "Tunnel.hpp"
#include <iostream>

Agent::Agent(boost::asio::io_context& io, AgentConfig cfg)
    : io_(io), control_socket_(io), cfg_(std::move(cfg)) {}

std::shared_ptr<Agent> Agent::Create(boost::asio::io_context& io, AgentConfig cfg) {
    return std::shared_ptr<Agent>(new Agent(io, std::move(cfg)));
}

void Agent::Start() {
    if (started_ || stopped_) return;
    started_ = true;

    if (!cfg_.public_port || !cfg_.server_control.port() ||
        !cfg_.server_proxy.port() || !cfg_.app.port()) {
        std::cerr << "Invalid agent port\n";
        Stop();
        return;
    }

    ConnectControl();
}

void Agent::Stop() {
    if (stopped_) return;
    stopped_ = true;

    boost::system::error_code ec;
    control_socket_.close(ec);
    for (const auto& tunnel : pending_) {
        tunnel->app.close(ec);
        tunnel->proxy.close(ec);
    }
    pending_.clear();
}

void Agent::ConnectControl() {
    auto self = shared_from_this();
    control_socket_.async_connect(cfg_.server_control,
        [this, self](const boost::system::error_code& ec) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Control connect: " << ec.message() << '\n';
                Stop();
                return;
            }

            SendPublicPort();
        }
    );
}

void Agent::SendPublicPort() {
    port_buf_[0] = static_cast<uint8_t>(cfg_.public_port >> 8);
    port_buf_[1] = static_cast<uint8_t>(cfg_.public_port);

    auto self = shared_from_this();
    boost::asio::async_write(control_socket_, boost::asio::buffer(port_buf_),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Send public port: " << ec.message() << '\n';
                Stop();
                return;
            }

            ReadReply();
        }
    );
}

void Agent::ReadReply() {
    auto self = shared_from_this();
    boost::asio::async_read(control_socket_, boost::asio::buffer(&command_, 1),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Registration reply: " << ec.message() << '\n';
                Stop();
                return;
            }
            if (command_ != 0) {
                std::cerr << "Public port rejected\n";
                Stop();
                return;
            }

            ReadTrigger();
        }
    );
}

void Agent::ReadTrigger() {
    auto self = shared_from_this();
    boost::asio::async_read(control_socket_, boost::asio::buffer(&command_, 1),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Read trigger: " << ec.message() << '\n';
                Stop();
                return;
            }

            OpenTunnel();
            ReadTrigger();
        }
    );
}

void Agent::OpenTunnel() {
    auto self = shared_from_this();
    auto tunnel = std::make_shared<PendingTunnel>(io_);
    pending_.push_back(tunnel);

    tunnel->app.async_connect(cfg_.app,
        [this, self, tunnel](const boost::system::error_code& ec) {
            if (stopped_) return;
            if (ec) {
                pending_.remove(tunnel);
                std::cerr << "App connect: " << ec.message() << '\n';
                return;
            }

            tunnel->proxy.async_connect(cfg_.server_proxy,
                [this, self, tunnel](const boost::system::error_code& ec) {
                    if (stopped_) return;
                    pending_.remove(tunnel);
                    if (ec) {
                        std::cerr << "Proxy connect: " << ec.message() << '\n';
                        return;
                    }

                    Tunnel::Create(std::move(tunnel->app), std::move(tunnel->proxy))->Start();
                }
            );
        }
    );
}
