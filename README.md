# PortForwarder

Agent publishes one local TCP service through a server.

Build the libraries and the two executables with CMake 3.23 or newer, a C++17
compiler, and Boost.System installed:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --target agent server
```

The entry points are `bin/agent.cpp` and `bin/server.cpp`. The executables are
written to `build/bin/` with a single-configuration generator.

```sh
# On the server: control port 7000, proxy port 7001.
./build/bin/server 7000 7001

# On the agent: publish local 127.0.0.1:8080 as server port 9000.
# Replace 192.0.2.10 with the server's IPv4 address.
./build/bin/agent 192.0.2.10 7000 7001 127.0.0.1 8080 9000
```

The server accepts an optional bind address applied to all three listeners:
`./build/bin/server 7000 7001 127.0.0.1`. Without it, listeners bind to `0.0.0.0`.
Addresses must be numeric IPv4 addresses, and ports must be in `1..65535`.
Run either executable with `--help` for its argument order. Stop it with Ctrl+C.
Each process serves one Agent/Server session and exits on a control failure;
restart the server before connecting another Agent.

```cpp
boost::asio::io_context io;
auto agent = Agent::Create(io, cfg);
agent->Start();
io.run();
```

Use one thread for `io_context`, `Start()` and `Stop()`.
Repeated `Start()` calls do nothing. A stopped Agent cannot restart.
`Stop()` closes control and pending connections; active tunnels continue independently.

Control protocol:

- Agent sends the public port as two bytes, most significant byte first.
- Server replies with one byte: `0` accepts registration, anything else rejects it.
- Each subsequent byte from the server requests one tunnel.

Agent connects to the local app first, then to the proxy endpoint.
Proxy connections carry only application data; the server pairs them with waiting clients in order.
Control errors stop Agent. Individual tunnel connection errors leave Agent running.

Server accepts one Agent and publishes the port requested during registration.

```cpp
boost::asio::io_context io;
auto server = Server::Create(io, {7000, 7001}); // control, proxy
server->Start();
io.run();
```

Server listens on all IPv4 interfaces by default. Set `control_bind_address`,
`proxy_bind_address`, and `public_bind_address` independently to choose local IPv4 addresses:

```cpp
Server::ServerConfig cfg{7000, 7001};
cfg.control_bind_address = boost::asio::ip::make_address_v4("127.0.0.1");
cfg.proxy_bind_address = boost::asio::ip::make_address_v4("127.0.0.1");
cfg.public_bind_address = boost::asio::ip::address_v4::any(); // 0.0.0.0: all IPv4 interfaces
auto server = Server::Create(io, cfg);
```

Use an IPv4 address assigned to the server to restrict a listener to that address.
`127.0.0.1` permits only local connections; an Agent on another machine needs a
reachable server address for the control and proxy listeners. Each field defaults
to the wildcard address, which can also be set with `make_address_v4("0.0.0.0")`.
The existing `{control_port, proxy_port}` and `{control_port, proxy_port, timeout}`
configuration forms keep their defaults.

Public and proxy listeners open during registration.
Waiting clients are paired with proxy connections in order and time out after 30 seconds by default.
The third config field sets this timeout, for example `std::chrono::seconds{10}`.
Proxy connections without waiting clients are closed.
Server uses the same single-thread and lifecycle rules as Agent.
Control disconnects stop Server. Active tunnels survive `Stop()`.
