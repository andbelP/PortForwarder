#include "common/relay/tcp_relay.hpp"

#include <boost/asio/write.hpp>

namespace pf {

TcpRelay::TcpRelay(tcp::socket socket_a, tcp::socket socket_b)
    : socket_a_(std::move(socket_a))
    , socket_b_(std::move(socket_b))
{}

void TcpRelay::Start()
{
    if (started_) return;
    started_ = true;
    ReadFromA();
    ReadFromB();
}

void TcpRelay::Close()
{
    Stop();
}

void TcpRelay::ReadFromA()
{
    if (stopped_) return;
    auto self = shared_from_this();
    socket_a_.async_read_some(
        boost::asio::buffer(buffer_a_to_b_),
        [this, self](const boost::system::error_code& ec, std::size_t n){
            if (ec) { Stop(); return; }
            WriteToB(n);
        }
    );
}

void TcpRelay::WriteToB(std::size_t size)
{
    if (stopped_) return;
    auto self = shared_from_this();
    boost::asio::async_write(
        socket_b_,
        boost::asio::buffer(buffer_a_to_b_.data(), size),
        [this, self](const boost::system::error_code& ec, std::size_t /*n*/){
            if (ec) { Stop(); return; }
            ReadFromA();
        }
    );
}

void TcpRelay::ReadFromB()
{
    if (stopped_) return;
    auto self = shared_from_this();
    socket_b_.async_read_some(
        boost::asio::buffer(buffer_b_to_a_),
        [this, self](const boost::system::error_code& ec, std::size_t n){
            if (ec) { Stop(); return; }
            WriteToA(n);
        }
    );
}

void TcpRelay::WriteToA(std::size_t size)
{
    if (stopped_) return;
    auto self = shared_from_this();
    boost::asio::async_write(
        socket_a_,
        boost::asio::buffer(buffer_b_to_a_.data(), size),
        [this, self](const boost::system::error_code& ec, std::size_t /*n*/){
            if (ec) { Stop(); return; }
            ReadFromB();
        }
    );
}

void TcpRelay::Stop()
{
    if (stopped_) return;
    stopped_ = true;
    boost::system::error_code ignored;
    socket_a_.cancel(ignored);
    socket_b_.cancel(ignored);
    socket_a_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_b_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_a_.close(ignored);
    socket_b_.close(ignored);
}

} // namespace pf
