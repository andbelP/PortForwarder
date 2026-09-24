#include "Tunnel.hpp"

void Tunnel::Forward(){

    namespace asio = boost::asio;

    auto self = shared_from_this();
    auto buffer_ptr = std::make_shared<std::array<char, 1024*4>>();
    a_.async_read_some(
        asio::buffer(*buffer_ptr),
        [this, self, buffer_ptr](const boost::system::error_code& ec, size_t bytes_read){

            asio::async_write(
                b_,
                asio::buffer(*buffer_ptr, bytes_read),
                [this, self, buffer_ptr](const boost::system::error_code& ec, size_t n){
                    Forward();
                }
            );

        }
    );
}

void Tunnel::Backward(){

    namespace asio = boost::asio;

    auto self = shared_from_this();
    auto buffer_ptr = std::make_shared<std::array<char, 1024*4>>();
    b_.async_read_some(
        asio::buffer(*buffer_ptr),
        [this, self, buffer_ptr](const boost::system::error_code& ec, size_t bytes_read){

            asio::async_write(
                a_,
                asio::buffer(*buffer_ptr, bytes_read),
                [this, self, buffer_ptr](const boost::system::error_code& ec, size_t n){
                    Backward();
                }
            );

        }
    );
}

void Tunnel::Start() {
    Forward();
    Backward();
}


void Tunnel::Close() {
    if (!closed_) {
        closed_ = true;
        boost::system::error_code ec;
        a_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        a_.close(ec);
        b_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        b_.close(ec);
    }
}

std::shared_ptr<Tunnel> Tunnel::Create(tcp::socket&& a, tcp::socket&& b) {
    return std::shared_ptr<Tunnel>(new Tunnel(std::move(a), std::move(b)));
}