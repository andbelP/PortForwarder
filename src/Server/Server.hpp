#pragma once

#include <boost/asio.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>

class Server : public std::enable_shared_from_this<Server> {
public:
    using tcp = boost::asio::ip::tcp;

    struct ServerConfig {
        uint16_t control_port = 0;
        uint16_t proxy_port = 0;
        std::chrono::seconds wait_timeout{30};
    };

    static std::shared_ptr<Server> Create(boost::asio::io_context& io, ServerConfig cfg);

    void Start();
    void Stop();

    bool IsStarted() const noexcept { return started_; }
    bool IsStopped() const noexcept { return stopped_; }

private:
    struct PendingClient {
        PendingClient(boost::asio::io_context& io, tcp::socket socket)
            : socket(std::move(socket)), timer(io) {}

        tcp::socket socket;
        boost::asio::steady_timer timer;
    };

    Server(boost::asio::io_context& io, ServerConfig cfg);

    bool Listen(tcp::acceptor& acceptor, uint16_t port);
    void AcceptControl();
    void ReadPublicPort();
    void SendReply(uint8_t status);
    void AcceptPublic();
    void AcceptProxy();
    void SendTrigger();
    void ReadControl();

    boost::asio::io_context& io_;
    ServerConfig cfg_;
    tcp::socket control_socket_;
    tcp::acceptor control_acceptor_;
    tcp::acceptor proxy_acceptor_;
    tcp::acceptor public_acceptor_;
    std::list<std::shared_ptr<PendingClient>> pending_;
    std::array<uint8_t, 2> port_buf_{};
    uint8_t reply_ = 0;
    uint8_t trigger_ = 1;
    uint8_t control_byte_ = 0;
    size_t triggers_ = 0;

    bool started_ = false;
    bool stopped_ = false;
};
