#pragma once

#include <boost/asio.hpp>
#include <array>
#include <memory>

class Tunnel : public std::enable_shared_from_this<Tunnel> {
public:

    using tcp = boost::asio::ip::tcp;

    static std::shared_ptr<Tunnel> Create(tcp::socket&& a, tcp::socket&& b);

    void Start();

private:

    using Buffer = std::array<char, 4096>;

    Tunnel(tcp::socket a, tcp::socket b);

    Tunnel(const Tunnel&)            = delete;
    Tunnel& operator=(const Tunnel&) = delete;

    void Forward(tcp::socket& from, tcp::socket& to, Buffer& buffer);
    void Close();

    tcp::socket a_;
    tcp::socket b_;

    Buffer a_buffer_{};
    Buffer b_buffer_{};

    bool started_ = false;
    bool closed_ = false;
};
