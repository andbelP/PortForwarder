#include "Agent.hpp"
#include "Server.hpp"
#include "Tunnel.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;
using namespace std::chrono_literals;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool would_block(const Error& error) {
    return error == asio::error::would_block || error == asio::error::try_again;
}

tcp::endpoint loopback(uint16_t port) {
    return {asio::ip::address_v4::loopback(), port};
}

std::string bytes(std::initializer_list<unsigned int> values) {
    std::string result;
    for (auto value : values) result.push_back(static_cast<char>(value));
    return result;
}

std::string payload(size_t size, unsigned int seed = 0) {
    std::string result(size, '\0');
    for (size_t i = 0; i < size; ++i)
        result[i] = static_cast<char>((i * 73 + seed) % 256);
    return result;
}

// Every handler and public lifecycle call runs on this thread. Socket helpers
// are nonblocking and pump the same context, with deadlines for all waits.
struct Harness {
    asio::io_context io;

    void tick() {
        if (io.stopped()) io.restart();
        io.run_for(1ms);
    }

    template <class Predicate>
    void until(Predicate predicate, const std::string& message,
               std::chrono::milliseconds timeout = 3000ms) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate()) {
            check(std::chrono::steady_clock::now() < deadline, message);
            tick();
        }
    }

    void settle(std::chrono::milliseconds duration = 25ms) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) tick();
    }

    tcp::acceptor listener(uint16_t port = 0) {
        tcp::acceptor result(io, loopback(port));
        result.non_blocking(true);
        return result;
    }

    tcp::socket accept(tcp::acceptor& listener) {
        tcp::socket result(io);
        until([&] {
            Error error;
            listener.accept(result, error);
            if (would_block(error)) return false;
            check(!error, "accept: " + error.message());
            return true;
        }, "timed out accepting a connection");
        result.non_blocking(true);
        return result;
    }

    std::pair<tcp::socket, Error> try_connect(tcp::endpoint endpoint) {
        tcp::socket result(io);
        struct Completion { bool done = false; Error error; };
        auto completion = std::make_shared<Completion>();
        result.async_connect(endpoint, [completion](Error error) {
            completion->error = error;
            completion->done = true;
        });
        until([&] { return completion->done; }, "timed out connecting");
        if (!completion->error) result.non_blocking(true);
        return {std::move(result), completion->error};
    }

    std::pair<tcp::socket, Error> try_connect(uint16_t port) {
        return try_connect(loopback(port));
    }

    tcp::socket connect(tcp::endpoint endpoint) {
        auto result = try_connect(endpoint);
        check(!result.second, "connect: " + result.second.message());
        return std::move(result.first);
    }

    tcp::socket connect(uint16_t port) { return connect(loopback(port)); }

    tcp::socket connect_when_listening(uint16_t port) {
        tcp::socket result(io);
        until([&] {
            auto attempt = try_connect(port);
            if (attempt.second == asio::error::connection_refused) return false;
            check(!attempt.second, "connect: " + attempt.second.message());
            result = std::move(attempt.first);
            return true;
        }, "listener did not become available");
        return result;
    }

    void refused(tcp::endpoint endpoint) {
        auto result = try_connect(endpoint);
        check(result.second == asio::error::connection_refused,
              "expected connection refused, got " + result.second.message());
    }

    void refused(uint16_t port) { refused(loopback(port)); }

    void write(tcp::socket& socket, const std::string& data) {
        size_t offset = 0;
        until([&] {
            if (offset == data.size()) return true;
            Error error;
            offset += socket.write_some(asio::buffer(data.data() + offset,
                                                    data.size() - offset), error);
            if (would_block(error)) return false;
            check(!error, "write: " + error.message());
            return offset == data.size();
        }, "timed out writing application data");
    }

    std::string read(tcp::socket& socket, size_t size) {
        std::string result(size, '\0');
        size_t offset = 0;
        until([&] {
            if (offset == size) return true;
            Error error;
            offset += socket.read_some(asio::buffer(result.data() + offset,
                                                   size - offset), error);
            if (would_block(error)) return false;
            check(!error, "read: " + error.message());
            return offset == size;
        }, "timed out reading " + std::to_string(size) + " bytes");
        return result;
    }

    void closed(tcp::socket& socket) {
        until([&] {
            char value;
            Error error;
            const auto count = socket.read_some(asio::buffer(&value, 1), error);
            if (would_block(error)) return false;
            check(count == 0, "received unexpected data while waiting for closure");
            check(error == asio::error::eof || error == asio::error::connection_reset,
                  "expected peer closure, got " + error.message());
            return true;
        }, "peer did not close the connection");
    }

    void quiet(tcp::socket& socket) {
        settle();
        char value;
        Error error;
        const auto count = socket.read_some(asio::buffer(&value, 1), error);
        check(count == 0 && would_block(error), "expected an open, idle connection");
    }

    void no_connection(tcp::acceptor& listener) {
        settle();
        tcp::socket unexpected(io);
        Error error;
        listener.accept(unexpected, error);
        check(would_block(error), "unexpected incoming connection");
    }

    void exchange(tcp::socket& a, tcp::socket& b, size_t size = 4097) {
        const auto forward = payload(size, 17);
        const auto reverse = payload(size, 93);
        // Queue both directions before reading either to exercise full duplex.
        write(a, forward);
        write(b, reverse);
        check(read(b, size) == forward, "forward payload was corrupted");
        check(read(a, size) == reverse, "reverse payload was corrupted");
    }
};

struct AgentFixture : Harness {
    tcp::acceptor control_listener = listener();
    tcp::acceptor proxy_listener = listener();
    tcp::acceptor app_listener = listener();
    Agent::AgentConfig config{control_listener.local_endpoint(),
                              proxy_listener.local_endpoint(),
                              app_listener.local_endpoint(), 0x1234};
    std::shared_ptr<Agent> agent;
    tcp::socket control{io};

    ~AgentFixture() { if (agent) agent->Stop(); }

    void create() { agent = Agent::Create(io, config); }

    void start() {
        create();
        agent->Start();
        control = accept(control_listener);
        const auto registration = read(control, 2);
        check(registration == bytes({static_cast<unsigned>(config.public_port >> 8),
                                     static_cast<unsigned>(config.public_port & 255)}),
              "public port must be encoded most significant byte first");
    }

    void registered() {
        start();
        write(control, bytes({0}));
    }

    void trigger(unsigned int value = 1) { write(control, bytes({value})); }

    std::pair<tcp::socket, tcp::socket> tunnel() {
        trigger();
        auto app = accept(app_listener);
        auto proxy = accept(proxy_listener);
        return {std::move(app), std::move(proxy)};
    }
};

void agent_lifecycle(const std::string& scenario) {
    AgentFixture f;
    f.create();
    check(!f.agent->IsStarted() && !f.agent->IsStopped(), "incorrect initial state");
    if (scenario == "initial") return;
    if (scenario == "stop_before_start") {
        f.agent->Stop();
        f.agent->Start();
        check(!f.agent->IsStarted() && f.agent->IsStopped(), "stopped Agent restarted");
        f.no_connection(f.control_listener);
        return;
    }
    f.agent->Start();
    if (scenario == "repeated_start") {
        for (int i = 0; i < 5; ++i) f.agent->Start();
        f.control = f.accept(f.control_listener);
        check(f.read(f.control, 2) == bytes({0x12, 0x34}), "registration changed");
        f.write(f.control, bytes({0}));
        f.quiet(f.control);
        f.no_connection(f.control_listener);
        check(f.agent->IsStarted() && !f.agent->IsStopped(), "repeated Start stopped Agent");
        return;
    }
    f.agent->Stop();
    if (scenario == "repeated_stop") {
        for (int i = 0; i < 5; ++i) f.agent->Stop();
    } else {
        f.agent->Start();
    }
    f.settle();
    check(f.agent->IsStarted() && f.agent->IsStopped(), "incorrect terminal state");
    // The initial asynchronous connect may already have reached the listener;
    // it must never carry registration after Stop().
    tcp::socket peer(f.io);
    Error error;
    f.control_listener.accept(peer, error);
    if (!error) { peer.non_blocking(true); f.closed(peer); }
    else check(would_block(error), "accept failed unexpectedly");
    f.no_connection(f.control_listener);
}

void agent_invalid_port(const std::string& field) {
    AgentFixture f;
    if (field == "public") f.config.public_port = 0;
    if (field == "control") f.config.server_control.port(0);
    if (field == "proxy") f.config.server_proxy.port(0);
    if (field == "app") f.config.app.port(0);
    f.create();
    f.agent->Start();
    check(f.agent->IsStarted() && f.agent->IsStopped(), "zero port was accepted");
    f.no_connection(f.control_listener);
}

void agent_control_error(const std::string& scenario) {
    AgentFixture f;
    if (scenario == "refused") {
        f.control_listener.close();
        f.create();
        f.agent->Start();
    } else if (scenario == "stop_connecting") {
        f.create();
        f.agent->Start();
        f.agent->Stop();
    } else {
        f.start();
        const bool after_reply = scenario == "stop_trigger" || scenario == "disconnect_trigger";
        if (after_reply) {
            f.write(f.control, bytes({0}));
            f.settle();
        }
        if (scenario == "stop_reply" || scenario == "stop_trigger") {
            f.agent->Stop();
            f.closed(f.control);
        } else {
            f.control.close();
        }
    }
    f.until([&] { return f.agent->IsStopped(); }, "control failure did not stop Agent");
    f.agent->Start();
    check(f.agent->IsStopped(), "Agent restarted after control failure");
    f.no_connection(f.app_listener);
    f.no_connection(f.proxy_listener);
}

void agent_port_encoding(uint16_t port) {
    AgentFixture f;
    f.config.public_port = port;
    f.start(); // Checks both exact bytes, including NUL and high-bit values.
    f.write(f.control, bytes({0}));
    f.quiet(f.control);
    check(!f.agent->IsStopped(), "valid registration stopped Agent");
}

void agent_rejection(unsigned int status) {
    AgentFixture f;
    f.start();
    f.write(f.control, bytes({status}));
    f.until([&] { return f.agent->IsStopped(); }, "nonzero reply did not stop Agent");
    f.closed(f.control);
    f.no_connection(f.app_listener);
}

void agent_trigger(unsigned int value) {
    AgentFixture f;
    f.registered();
    f.trigger(value);
    auto app = f.accept(f.app_listener);
    auto proxy = f.accept(f.proxy_listener);
    f.exchange(app, proxy, 256);
    f.no_connection(f.app_listener);
    f.no_connection(f.proxy_listener);
    check(!f.agent->IsStopped(), "trigger stopped Agent");
}

void agent_no_trigger() {
    AgentFixture f;
    f.registered();
    f.no_connection(f.app_listener);
    f.no_connection(f.proxy_listener);
    f.quiet(f.control);
    check(!f.agent->IsStopped(), "idle Agent stopped");
}

void agent_app_before_proxy() {
    AgentFixture f;
    f.app_listener.close();
    f.registered();
    f.trigger();
    f.no_connection(f.proxy_listener);
    check(!f.agent->IsStopped(), "failed app connection stopped Agent");
    // Once the app is reachable, the same control channel can open a tunnel.
    f.app_listener = f.listener(f.config.app.port());
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second, 256);
}

void agent_connection_failure(const std::string& side, bool recover) {
    AgentFixture f;
    const bool app_failure = side == "app";
    if (app_failure) f.app_listener.close();
    else f.proxy_listener.close();
    f.registered();
    f.trigger();
    if (app_failure) {
        f.no_connection(f.proxy_listener);
    } else {
        auto pending_app = f.accept(f.app_listener);
        f.closed(pending_app);
    }
    f.quiet(f.control);
    check(!f.agent->IsStopped(), "individual connection failure stopped Agent");
    if (recover) {
        if (app_failure) f.app_listener = f.listener(f.config.app.port());
        else f.proxy_listener = f.listener(f.config.server_proxy.port());
        auto peers = f.tunnel();
        f.exchange(peers.first, peers.second);
        check(!f.agent->IsStopped(), "Agent failed to recover on next trigger");
    }
}

void agent_batched_triggers() {
    AgentFixture f;
    f.registered();
    f.write(f.control, bytes({0, 1, 128, 255}));
    std::vector<tcp::socket> apps, proxies;
    for (int i = 0; i < 4; ++i) {
        apps.push_back(f.accept(f.app_listener));
        proxies.push_back(f.accept(f.proxy_listener));
    }
    // Concurrent connects need not complete in the same order. Check the
    // complete set of streams instead of imposing an undocumented pairing.
    for (auto& app : apps) f.write(app, "x");
    for (auto& proxy : proxies) check(f.read(proxy, 1) == "x", "lost trigger");
    f.no_connection(f.app_listener);
    f.no_connection(f.proxy_listener);
}

void agent_stop_pending() {
    AgentFixture f;
    f.registered();
    f.quiet(f.control); // Finish registration before selecting the trigger handler.
    f.trigger();
    // Process only the trigger handler: it starts an app connection, whose
    // completion cannot run until this thread services the context again.
    if (f.io.stopped()) f.io.restart();
    check(f.io.run_one_for(3000ms) == 1, "trigger handler did not run");
    f.agent->Stop();
    auto app = f.accept(f.app_listener);
    f.closed(app);
    f.closed(f.control);
    f.no_connection(f.proxy_listener);
}

void agent_active_survival(bool control_loss) {
    AgentFixture f;
    f.registered();
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second, 1); // Establish active forwarding.
    if (control_loss) {
        f.control.close();
        f.until([&] { return f.agent->IsStopped(); }, "Agent ignored control loss");
    } else {
        f.agent->Stop();
        f.closed(f.control);
    }
    f.agent.reset(); // The tunnel must also be independent of Agent lifetime.
    f.exchange(peers.first, peers.second, 65536);
}

void agent_proxy_data_only() {
    AgentFixture f;
    f.registered();
    auto peers = f.tunnel();
    f.quiet(peers.second);
    const auto data = bytes({0, 0x12, 0x34, 255, 1});
    f.write(peers.first, data);
    check(f.read(peers.second, data.size()) == data, "proxy stream contains protocol bytes");
    f.quiet(peers.second);
}

// Reserve distinct ephemeral ports until immediately before the relevant
// production listener binds. No fixed ports or external services are needed.
struct ServerFixture : Harness {
    tcp::acceptor control_reservation = listener();
    tcp::acceptor proxy_reservation = listener();
    tcp::acceptor public_reservation = listener();
    uint16_t control_port = control_reservation.local_endpoint().port();
    uint16_t proxy_port = proxy_reservation.local_endpoint().port();
    uint16_t public_port = public_reservation.local_endpoint().port();
    Server::ServerConfig config{control_port, proxy_port};
    std::shared_ptr<Server> server;
    tcp::socket control{io};

    ~ServerFixture() { if (server) server->Stop(); }

    void create() { server = Server::Create(io, config); }

    void start() {
        control_reservation.close();
        create();
        server->Start();
        check(!server->IsStopped(), "Server failed to start");
    }

    void connect_control() {
        start();
        control = connect(control_port);
    }

    void release_registration_ports() {
        if (public_reservation.is_open()) public_reservation.close();
        if (proxy_reservation.is_open()) proxy_reservation.close();
    }

    unsigned int registration_reply(uint16_t port) {
        write(control, bytes({static_cast<unsigned>(port >> 8),
                              static_cast<unsigned>(port & 255)}));
        return static_cast<unsigned char>(read(control, 1)[0]);
    }

    void registered() {
        connect_control();
        release_registration_ports();
        check(registration_reply(public_port) == 0, "registration was rejected");
    }

    tcp::socket client() {
        auto result = connect(public_port);
        check(read(control, 1) == bytes({1}), "missing one-byte tunnel trigger");
        return result;
    }

    std::pair<tcp::socket, tcp::socket> tunnel() {
        auto public_client = client();
        auto proxy = connect(proxy_port);
        return {std::move(public_client), std::move(proxy)};
    }
};

void server_lifecycle(const std::string& scenario) {
    ServerFixture f;
    f.create();
    check(!f.server->IsStarted() && !f.server->IsStopped(), "incorrect initial state");
    if (scenario == "initial") return;
    if (scenario == "stop_before_start") {
        f.server->Stop();
        f.control_reservation.close();
        f.server->Start();
        check(!f.server->IsStarted() && f.server->IsStopped(), "stopped Server restarted");
        f.refused(f.control_port);
        return;
    }
    f.control_reservation.close();
    f.server->Start();
    if (scenario == "repeated_start") {
        for (int i = 0; i < 5; ++i) f.server->Start();
        f.control = f.connect(f.control_port);
        f.release_registration_ports();
        check(f.registration_reply(f.public_port) == 0, "repeated Start broke registration");
        auto peers = f.tunnel();
        f.exchange(peers.first, peers.second);
        return;
    }
    f.server->Stop();
    if (scenario == "repeated_stop") {
        for (int i = 0; i < 5; ++i) f.server->Stop();
    } else f.server->Start();
    check(f.server->IsStarted() && f.server->IsStopped(), "incorrect terminal state");
    f.refused(f.control_port);
}

void server_invalid_config(const std::string& field) {
    ServerFixture f;
    if (field == "control") f.config.control_port = 0;
    if (field == "proxy") f.config.proxy_port = 0;
    if (field == "same_ports") f.config.proxy_port = f.config.control_port;
    if (field == "zero_timeout") f.config.wait_timeout = 0s;
    if (field == "negative_timeout") f.config.wait_timeout = -1s;
    f.control_reservation.close();
    f.create();
    f.server->Start();
    check(f.server->IsStarted() && f.server->IsStopped(), "invalid Server config accepted");
    f.refused(f.control_port);
}

void server_default_timeout() {
    ServerFixture f;
    check(f.config.wait_timeout == 30s, "default waiting timeout is not 30 seconds");
    check(f.config.control_bind_address.is_unspecified() &&
          f.config.proxy_bind_address.is_unspecified() &&
          f.config.public_bind_address.is_unspecified(), "default bind addresses are not wildcard");
    f.registered();
    auto client = f.client();
    f.quiet(client);
    auto proxy = f.connect(f.proxy_port);
    f.exchange(client, proxy);
}

void server_occupied_port(const std::string& side) {
    ServerFixture f;
    if (side == "control") {
        f.create();
        f.server->Start();
        check(f.server->IsStopped(), "Server accepted occupied control port");
        return;
    }
    f.connect_control();
    if (side == "public") f.proxy_reservation.close();
    else f.public_reservation.close();
    check(f.registration_reply(f.public_port) != 0, "occupied listener port was accepted");
    f.until([&] { return f.server->IsStopped(); }, "rejected registration did not stop Server");
    f.closed(f.control);
    // A partially opened listener must be cleaned up after rejection.
    if (side == "proxy") f.refused(f.public_port);
    else f.refused(f.proxy_port);
}

void server_before_registration(const std::string& side) {
    ServerFixture f;
    f.connect_control();
    f.release_registration_ports();
    f.refused(side == "public" ? f.public_port : f.proxy_port);
    check(f.registration_reply(f.public_port) == 0, "registration failed");
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second);
}

void server_fragmented_registration() {
    ServerFixture f;
    f.connect_control();
    f.release_registration_ports();
    f.write(f.control, bytes({static_cast<unsigned>(f.public_port >> 8)}));
    f.quiet(f.control);
    f.refused(f.public_port);
    f.refused(f.proxy_port);
    f.write(f.control, bytes({static_cast<unsigned>(f.public_port & 255)}));
    check(f.read(f.control, 1) == bytes({0}), "fragmented registration was not accepted");
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second);
}

void server_registration(const std::string& scenario) {
    ServerFixture f;
    f.connect_control();
    f.release_registration_ports();
    uint16_t port = f.public_port;
    if (scenario == "zero") port = 0;
    if (scenario == "control") port = f.control_port;
    if (scenario == "proxy") port = f.proxy_port;
    const auto reply = f.registration_reply(port);
    if (scenario == "valid") {
        check(reply == 0, "valid registration was rejected");
        auto peers = f.tunnel();
        f.exchange(peers.first, peers.second);
        f.refused(f.control_port);
    } else {
        check(reply != 0, "invalid public port was accepted");
        f.until([&] { return f.server->IsStopped(); }, "Server did not stop after rejection");
        f.closed(f.control);
        f.refused(f.public_port);
        f.refused(f.proxy_port);
    }
}

void server_one_agent() {
    ServerFixture f;
    f.connect_control();
    // Even before registration completes, accepting the first control socket
    // closes the control listener.
    f.settle();
    f.refused(f.control_port);
    f.release_registration_ports();
    check(f.registration_reply(f.public_port) == 0, "first Agent could not register");
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second);
}

void server_control_loss(bool registered) {
    ServerFixture f;
    if (registered) f.registered();
    else {
        f.connect_control();
        f.release_registration_ports();
        f.write(f.control, bytes({static_cast<unsigned>(f.public_port >> 8)}));
    }
    f.control.close();
    f.until([&] { return f.server->IsStopped(); }, "control loss did not stop Server");
    f.refused(f.control_port);
    f.refused(f.public_port);
    f.refused(f.proxy_port);
    f.server->Start();
    check(f.server->IsStopped(), "Server restarted after control loss");
}

void server_unexpected_control_data() {
    ServerFixture f;
    f.registered();
    auto client = f.client();
    f.write(f.control, bytes({255}));
    f.until([&] { return f.server->IsStopped(); }, "unexpected control data was ignored");
    f.closed(client);
    f.closed(f.control);
    f.refused(f.public_port);
    f.refused(f.proxy_port);
}

void server_unsolicited_proxy() {
    ServerFixture f;
    f.registered();
    auto unsolicited = f.connect(f.proxy_port);
    f.closed(unsolicited);
    check(!f.server->IsStopped(), "unsolicited proxy stopped Server");
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second);
}

void server_fifo() {
    ServerFixture f;
    f.registered();
    std::vector<tcp::socket> clients, proxies;
    for (int i = 0; i < 4; ++i) {
        clients.push_back(f.client());
        f.write(clients.back(), payload(256, i));
    }
    for (int i = 0; i < 4; ++i) {
        proxies.push_back(f.connect(f.proxy_port));
        check(f.read(proxies.back(), 256) == payload(256, i), "waiting clients paired out of order");
        f.write(proxies.back(), payload(256, i + 10));
        check(f.read(clients[i], 256) == payload(256, i + 10), "reverse FIFO pairing failed");
    }
    f.quiet(f.control); // Exactly one trigger per public client.
}

void server_timeout() {
    ServerFixture f;
    f.config.wait_timeout = 1s;
    f.registered();
    auto expired = f.client();
    const auto start = std::chrono::steady_clock::now();
    f.closed(expired);
    check(std::chrono::steady_clock::now() - start >= 800ms, "client expired too early");
    check(!f.server->IsStopped(), "waiting timeout stopped Server");
    auto unsolicited = f.connect(f.proxy_port);
    f.closed(unsolicited); // Expired clients must be removed from the FIFO.
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second);
    f.settle(1100ms); // Pairing cancels the waiting timer.
    f.exchange(peers.first, peers.second, 256);
}

void server_stop_pending() {
    ServerFixture f;
    f.registered();
    std::vector<tcp::socket> clients;
    for (int i = 0; i < 4; ++i) clients.push_back(f.client());
    f.server->Stop();
    for (auto& client : clients) f.closed(client);
    f.closed(f.control);
    f.refused(f.public_port);
    f.refused(f.proxy_port);
}

void server_active_survival() {
    ServerFixture f;
    f.registered();
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second, 1);
    auto pending = f.client();
    f.server->Stop();
    f.closed(pending);
    f.closed(f.control);
    f.server.reset();
    f.exchange(peers.first, peers.second, 65536);
    f.refused(f.public_port);
    f.refused(f.proxy_port);
}

void server_bind_address(const std::string& side, bool wildcard) {
    ServerFixture f;
    const auto alternate = asio::ip::make_address_v4("127.0.0.2");
    const auto bind_address = wildcard ? asio::ip::make_address_v4("0.0.0.0")
                                       : asio::ip::address_v4::loopback();
    const auto destination = wildcard ? alternate : bind_address;
    if (side == "control") f.config.control_bind_address = bind_address;
    if (side == "proxy") f.config.proxy_bind_address = bind_address;
    if (side == "public") f.config.public_bind_address = bind_address;
    f.start();
    if (side == "control" && !wildcard) f.refused({alternate, f.control_port});
    f.control = f.connect(side == "control" ? tcp::endpoint(destination, f.control_port)
                                            : loopback(f.control_port));
    f.release_registration_ports();
    check(f.registration_reply(f.public_port) == 0, "configured addresses broke registration");
    if (!wildcard && side != "control")
        f.refused({alternate, side == "public" ? f.public_port : f.proxy_port});
    auto client = f.connect(side == "public" ? tcp::endpoint(destination, f.public_port)
                                             : loopback(f.public_port));
    check(f.read(f.control, 1) == bytes({1}), "missing tunnel trigger");
    auto proxy = f.connect(side == "proxy" ? tcp::endpoint(destination, f.proxy_port)
                                           : loopback(f.proxy_port));
    f.exchange(client, proxy);
}

void server_independent_bind_addresses() {
    ServerFixture f;
    f.config.control_bind_address = asio::ip::make_address_v4("127.0.0.1");
    f.config.proxy_bind_address = asio::ip::make_address_v4("127.0.0.2");
    f.config.public_bind_address = asio::ip::make_address_v4("127.0.0.3");
    f.start();
    f.refused({f.config.proxy_bind_address, f.control_port});
    f.control = f.connect({f.config.control_bind_address, f.control_port});
    f.release_registration_ports();
    check(f.registration_reply(f.public_port) == 0, "independent addresses broke registration");
    f.refused({f.config.control_bind_address, f.proxy_port});
    f.refused({f.config.public_bind_address, f.proxy_port});
    f.refused({f.config.control_bind_address, f.public_port});
    f.refused({f.config.proxy_bind_address, f.public_port});
    auto client = f.connect({f.config.public_bind_address, f.public_port});
    check(f.read(f.control, 1) == bytes({1}), "missing tunnel trigger");
    auto proxy = f.connect({f.config.proxy_bind_address, f.proxy_port});
    f.exchange(client, proxy, 65536);
}

struct TunnelFixture : Harness {
    tcp::socket left{io};
    tcp::socket right{io};
    std::shared_ptr<Tunnel> tunnel;

    explicit TunnelFixture(bool start = true) {
        auto a = listener();
        auto b = listener();
        left = connect(a.local_endpoint().port());
        auto inside_a = accept(a);
        right = connect(b.local_endpoint().port());
        auto inside_b = accept(b);
        tunnel = Tunnel::Create(std::move(inside_a), std::move(inside_b));
        if (start) tunnel->Start();
    }
};

void tunnel_transfer(size_t size, bool reverse) {
    TunnelFixture f;
    auto& from = reverse ? f.right : f.left;
    auto& to = reverse ? f.left : f.right;
    const auto data = payload(size);
    f.write(from, data);
    check(f.read(to, size) == data, "tunnel corrupted application bytes");
    f.quiet(to); // No duplication or extra framing bytes.
}

void tunnel_duplex() {
    TunnelFixture f;
    f.tunnel.reset(); // Async operations retain the active tunnel themselves.
    f.exchange(f.left, f.right, 65536);
}

void tunnel_repeated_start() {
    TunnelFixture f(false);
    for (int i = 0; i < 5; ++i) f.tunnel->Start();
    f.exchange(f.left, f.right, 65536);
    f.quiet(f.left);
    f.quiet(f.right);
}

void tunnel_eof(bool reverse) {
    TunnelFixture f;
    f.exchange(f.left, f.right, 256);
    (reverse ? f.right : f.left).close();
    f.closed(reverse ? f.left : f.right);
}

void tunnel_invalid_socket() {
    Harness f;
    auto listener = f.listener();
    auto peer = f.connect(listener.local_endpoint().port());
    auto valid = f.accept(listener);
    auto tunnel = Tunnel::Create(tcp::socket(f.io), std::move(valid));
    tunnel->Start();
    f.closed(peer);
    tunnel->Start(); // Closed tunnels cannot restart either.
}

struct EndToEndFixture : ServerFixture {
    tcp::acceptor app_listener = listener();
    std::shared_ptr<Agent> agent;

    explicit EndToEndFixture(bool restrict_to_loopback = false) {
        if (restrict_to_loopback) {
            config.control_bind_address = asio::ip::address_v4::loopback();
            config.proxy_bind_address = asio::ip::address_v4::loopback();
            config.public_bind_address = asio::ip::address_v4::loopback();
        }
        start();
        release_registration_ports();
        agent = Agent::Create(io, {loopback(control_port), loopback(proxy_port),
                                   app_listener.local_endpoint(), public_port});
        agent->Start();
    }

    ~EndToEndFixture() { agent->Stop(); }

    std::pair<tcp::socket, tcp::socket> tunnel() {
        auto client = connect_when_listening(public_port);
        auto app = accept(app_listener);
        return {std::move(client), std::move(app)};
    }
};

void e2e_transfer(size_t size, bool restrict_to_loopback = false) {
    EndToEndFixture f(restrict_to_loopback);
    auto peers = f.tunnel();
    const auto request = payload(size);
    const auto response = payload(size, 255);
    f.write(peers.first, request);
    check(f.read(peers.second, size) == request, "public-to-app payload was corrupted");
    f.write(peers.second, response);
    check(f.read(peers.first, size) == response, "app-to-public payload was corrupted");
    check(!f.agent->IsStopped() && !f.server->IsStopped(), "forwarding stopped control peers");
}

void e2e_duplex() {
    EndToEndFixture f;
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second, 65536);
}

void e2e_multiple_clients(bool sequential) {
    EndToEndFixture f;
    std::vector<std::pair<tcp::socket, tcp::socket>> peers;
    for (int i = 0; i < 4; ++i) {
        peers.push_back(f.tunnel());
        // Establish each stream before opening the next, leaving all four
        // active for the concurrent case. FIFO itself is tested with raw peers.
        f.exchange(peers.back().first, peers.back().second, 256);
        if (sequential) {
            peers.back().first.close();
            f.closed(peers.back().second);
        }
    }
    if (!sequential) {
        for (int i = 0; i < 4; ++i) f.write(peers[i].first, payload(4097, i));
        for (int i = 0; i < 4; ++i) {
            check(f.read(peers[i].second, 4097) == payload(4097, i), "client streams were mixed");
            f.write(peers[i].second, payload(4097, i + 31));
        }
        for (int i = 0; i < 4; ++i)
            check(f.read(peers[i].first, 4097) == payload(4097, i + 31), "response streams were mixed");
    }
    check(!f.agent->IsStopped() && !f.server->IsStopped(), "client lifecycle stopped control peers");
}

void e2e_survival(const std::string& side) {
    EndToEndFixture f;
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second, 1);
    if (side == "agent" || side == "both") f.agent->Stop();
    if (side == "server" || side == "both") f.server->Stop();
    f.until([&] { return f.agent->IsStopped() && f.server->IsStopped(); },
            "control disconnect did not stop both peers");
    f.exchange(peers.first, peers.second, 65536);
    f.refused(f.public_port);
    f.refused(f.proxy_port);
}

void e2e_disconnect(bool public_side) {
    EndToEndFixture f;
    auto peers = f.tunnel();
    f.exchange(peers.first, peers.second, 256);
    (public_side ? peers.first : peers.second).close();
    f.closed(public_side ? peers.second : peers.first);
    check(!f.agent->IsStopped() && !f.server->IsStopped(), "tunnel EOF stopped control peers");
    auto next = f.tunnel();
    f.exchange(next.first, next.second);
}

struct TestCase { const char* name; void (*run)(); };
const TestCase cases[] = {
#define PF_TEST(name, ...) {#name, [] { __VA_ARGS__; }},
#include "cases.inc"
#undef PF_TEST
};
static_assert(sizeof(cases) / sizeof(cases[0]) == 108, "expected exactly 108 tests");
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: portforwarder_tests <case-name>\n";
        return 2;
    }
    const auto test = std::find_if(std::begin(cases), std::end(cases),
                                   [&](const TestCase& item) { return item.name == std::string(argv[1]); });
    if (test == std::end(cases)) {
        std::cerr << "Unknown test case: " << argv[1] << '\n';
        return 2;
    }
    try {
        test->run();
        std::cout << "PASS " << test->name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << test->name << ": " << error.what() << '\n';
        return 1;
    }
}
