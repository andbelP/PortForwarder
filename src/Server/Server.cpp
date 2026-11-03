#include "Server.hpp"
#include "Tunnel.hpp"
#include <iostream>

Server::Server(boost::asio::io_context& io, ServerConfig cfg)
    : io_(io), cfg_(std::move(cfg)), control_socket_(io),
      control_acceptor_(io), proxy_acceptor_(io), public_acceptor_(io) {}

std::shared_ptr<Server> Server::Create(boost::asio::io_context& io, ServerConfig cfg) {
    return std::shared_ptr<Server>(new Server(io, std::move(cfg)));
}

void Server::Start() {
    if (started_ || stopped_) return;
    started_ = true;

    if (!cfg_.control_port || !cfg_.proxy_port ||
        cfg_.control_port == cfg_.proxy_port || cfg_.wait_timeout.count() <= 0) {
        std::cerr << "Invalid server config\n";
        Stop();
        return;
    }
    if (!Listen(control_acceptor_, cfg_.control_port)) {
        Stop();
        return;
    }

    AcceptControl();
}

void Server::Stop() {
    if (stopped_) return;
    stopped_ = true;

    boost::system::error_code ec;
    control_acceptor_.close(ec);
    public_acceptor_.close(ec);
    proxy_acceptor_.close(ec);
    control_socket_.close(ec);
    for (const auto& client : pending_) {
        client->timer.cancel();
        client->socket.close(ec);
    }
    pending_.clear();
}

bool Server::Listen(tcp::acceptor& acceptor, uint16_t port) {
    boost::system::error_code ec;
    acceptor.open(tcp::v4(), ec);
    if (!ec) acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    if (!ec) acceptor.bind(tcp::endpoint(tcp::v4(), port), ec);
    if (!ec) acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        std::cerr << "Listen on " << port << ": " << ec.message() << '\n';
        return false;
    }
    return true;
}

void Server::AcceptControl() {
    auto self = shared_from_this();
    control_acceptor_.async_accept(control_socket_,
        [this, self](const boost::system::error_code& ec) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Accept control: " << ec.message() << '\n';
                Stop();
                return;
            }

            boost::system::error_code ignored;
            control_acceptor_.close(ignored);
            ReadPublicPort();
        }
    );
}

void Server::ReadPublicPort() {
    auto self = shared_from_this();
    boost::asio::async_read(control_socket_, boost::asio::buffer(port_buf_),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Read public port: " << ec.message() << '\n';
                Stop();
                return;
            }

            uint16_t port = (uint16_t(port_buf_[0]) << 8) | port_buf_[1];
            if (!port || port == cfg_.control_port || port == cfg_.proxy_port ||
                !Listen(public_acceptor_, port) || !Listen(proxy_acceptor_, cfg_.proxy_port)) {
                SendReply(1);
                return;
            }

            SendReply(0);
        }
    );
}

void Server::SendReply(uint8_t status) {
    reply_ = status;
    auto self = shared_from_this();
    boost::asio::async_write(control_socket_, boost::asio::buffer(&reply_, 1),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec || reply_ != 0) {
                if (ec) std::cerr << "Send reply: " << ec.message() << '\n';
                Stop();
                return;
            }

            AcceptPublic();
            AcceptProxy();
            ReadControl();
        }
    );
}

void Server::AcceptPublic() {
    auto self = shared_from_this();
    public_acceptor_.async_accept(
        [this, self](const boost::system::error_code& ec, tcp::socket socket) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Accept public: " << ec.message() << '\n';
                Stop();
                return;
            }

            auto client = std::make_shared<PendingClient>(io_, std::move(socket));
            pending_.push_back(client);
            client->timer.expires_after(cfg_.wait_timeout);
            client->timer.async_wait([this, self, client](const boost::system::error_code& ec) {
                if (stopped_ || ec) return;
                pending_.remove(client);
                boost::system::error_code ignored;
                client->socket.close(ignored);
            });

            if (++triggers_ == 1) SendTrigger();
            AcceptPublic();
        }
    );
}

void Server::AcceptProxy() {
    auto self = shared_from_this();
    proxy_acceptor_.async_accept(
        [this, self](const boost::system::error_code& ec, tcp::socket socket) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Accept proxy: " << ec.message() << '\n';
                Stop();
                return;
            }

            if (!pending_.empty()) {
                auto client = pending_.front();
                pending_.pop_front();
                client->timer.cancel();
                Tunnel::Create(std::move(client->socket), std::move(socket))->Start();
            }

            AcceptProxy();
        }
    );
}

void Server::SendTrigger() {
    auto self = shared_from_this();
    boost::asio::async_write(control_socket_, boost::asio::buffer(&trigger_, 1),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec) {
                std::cerr << "Send trigger: " << ec.message() << '\n';
                Stop();
                return;
            }

            if (--triggers_ != 0) SendTrigger();
        }
    );
}

void Server::ReadControl() {
    auto self = shared_from_this();
    control_socket_.async_read_some(boost::asio::buffer(&control_byte_, 1),
        [this, self](const boost::system::error_code& ec, size_t) {
            if (stopped_) return;
            if (ec) std::cerr << "Read control: " << ec.message() << '\n';
            else std::cerr << "Unexpected control data\n";
            Stop();
        }
    );
}
