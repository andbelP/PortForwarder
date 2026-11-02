#include "Tunnel.hpp"

Tunnel::Tunnel(tcp::socket a, tcp::socket b) : a_(std::move(a)), b_(std::move(b)) {}

void Tunnel::Forward(tcp::socket& from, tcp::socket& to, Buffer& buffer) {
    if (closed_) return;

    namespace asio = boost::asio;

    auto self = shared_from_this();
    from.async_read_some(
        asio::buffer(buffer),
        [this, self, &from, &to, &buffer](const boost::system::error_code& ec, size_t bytes_read) {
            if (ec) {
                Close();
                return;
            }

            asio::async_write(
                to,
                asio::buffer(buffer, bytes_read),
                [this, self, &from, &to, &buffer](const boost::system::error_code& ec, size_t) {
                    if (ec) {
                        Close();
                        return;
                    }

                    Forward(from, to, buffer);
                }
            );
        }
    );
}

void Tunnel::Start() {
    if (started_ || closed_) return;
    started_ = true;

    if (!a_.is_open() || !b_.is_open()) {
        Close();
        return;
    }

    Forward(a_, b_, a_buffer_);
    Forward(b_, a_, b_buffer_);
}

void Tunnel::Close() {
    if (closed_) return;
    closed_ = true;

    boost::system::error_code ec;
    a_.shutdown(tcp::socket::shutdown_both, ec);
    a_.close(ec);
    b_.shutdown(tcp::socket::shutdown_both, ec);
    b_.close(ec);
}

std::shared_ptr<Tunnel> Tunnel::Create(tcp::socket&& a, tcp::socket&& b) {
    return std::shared_ptr<Tunnel>(new Tunnel(std::move(a), std::move(b)));
}
