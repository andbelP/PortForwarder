#include "Server.hpp"
#include "cli.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    const bool help = argc == 2 && std::string_view(argv[1]) == "--help";
    if (help || (argc != 3 && argc != 4)) {
        auto& output = help ? std::cout : std::cerr;
        output << "Usage: " << argv[0]
               << " <control-port> <proxy-port> [bind-ipv4]\n"
                  "Default bind address: 0.0.0.0 (all IPv4 interfaces).\n";
        return help ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    try {
        Server::ServerConfig cfg{ParsePort(argv[1]), ParsePort(argv[2])};
        if (argc == 4) {
            const auto address = boost::asio::ip::make_address_v4(argv[3]);
            cfg.control_bind_address = address;
            cfg.proxy_bind_address = address;
            cfg.public_bind_address = address;
        }

        boost::asio::io_context io;
        auto server = Server::Create(io, cfg);
        server->Start();
        io.run();
        return server->IsStopped() ? EXIT_FAILURE : EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Server: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
