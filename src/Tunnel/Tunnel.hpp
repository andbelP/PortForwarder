#pragma once

#include <boost/asio.hpp>
#include <memory>

class Tunnel : public std::enable_shared_from_this<Tunnel> {
public:

    using tcp = boost::asio::ip::tcp;

    static std::shared_ptr<Tunnel> Create(tcp::socket&& a, tcp::socket&& b);

    void Start();

private:


    Tunnel(tcp::socket a, tcp::socket b);

    Tunnel(const Tunnel&)            = delete;
    Tunnel& operator=(const Tunnel&) = delete;

    void Forward();
    void Backward();
    void Close();

    tcp::socket a_;
    tcp::socket b_;

    bool closed_ = false;
};