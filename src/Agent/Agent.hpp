#pragma once

#include <boost/asio.hpp>
#include <cstdint>
#include <memory>

class Agent : public std::enable_shared_from_this<Agent> {
   public:
    using tcp = boost::asio::ip::tcp;

    struct Config {
        tcp::endpoint server_control;
        tcp::endpoint server_proxy;
        tcp::endpoint app;
        uint16_t public_port;
    };

    static std::shared_ptr<Agent> Create(Config cfg);

    void Start();

   private:
    explicit Agent(Config cfg);

    void ConnectControl();
    void SendPublicPort();
    void ReadTrigger();

    void OpenTunnel();

    boost::asio::io_context io_;
    tcp::socket control_socket_{io_};
    Config cfg_;
    std::array<uint8_t, 1> trigger_buf_{};
};