#pragma once

#include <boost/asio.hpp>
#include <array>
#include <cstdint>
#include <list>
#include <memory>

class Agent : public std::enable_shared_from_this<Agent> {
public:
    using tcp = boost::asio::ip::tcp;

    struct AgentConfig {
        tcp::endpoint server_control;
        tcp::endpoint server_proxy;
        tcp::endpoint app;
        uint16_t public_port = 0;
    };

    static std::shared_ptr<Agent> Create(boost::asio::io_context& io, AgentConfig cfg);

    void Start();
    void Stop();

    bool IsStarted() const noexcept { return started_; }
    bool IsStopped() const noexcept { return stopped_; }

private:
    struct PendingTunnel {
        explicit PendingTunnel(boost::asio::io_context& io) : app(io), proxy(io) {}

        tcp::socket app;
        tcp::socket proxy;
    };

    Agent(boost::asio::io_context& io, AgentConfig cfg);

    void ConnectControl();
    void SendPublicPort();
    void ReadReply();
    void ReadTrigger();
    void OpenTunnel();

    boost::asio::io_context& io_;
    tcp::socket control_socket_;
    AgentConfig cfg_;
    std::array<uint8_t, 2> port_buf_{};
    uint8_t command_ = 0;
    std::list<std::shared_ptr<PendingTunnel>> pending_;

    bool started_ = false;
    bool stopped_ = false;
};
