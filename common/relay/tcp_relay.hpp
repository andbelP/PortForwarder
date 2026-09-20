// TcpRelay relays raw bytes between two connected TCP sockets.
// TcpRelay relays raw bytes between two connected TCP sockets.
#pragma once

#include <array>
#include <boost/asio.hpp>
#include <memory>

namespace pf {

class TcpRelay : public std::enable_shared_from_this<TcpRelay> {
   public:
    using tcp = boost::asio::ip::tcp;

    TcpRelay(tcp::socket socket_a, tcp::socket socket_b);

    void Start();
    void Close();

   private:
    static constexpr std::size_t buffer_size = 16 * 1024;

    void ReadFromA();
    void WriteToB(std::size_t size);
    void ReadFromB();
    void WriteToA(std::size_t size);
    void Stop();

    tcp::socket socket_a_;
    tcp::socket socket_b_;
    std::array<char, buffer_size> buffer_a_to_b_{};
    std::array<char, buffer_size> buffer_b_to_a_{};
    bool started_ = false;
    bool stopped_ = false;
};

}  // namespace pf
