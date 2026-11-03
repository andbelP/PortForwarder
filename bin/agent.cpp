#include "Agent.hpp"
#include "cli.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    const bool help = argc == 2 && std::string_view(argv[1]) == "--help";
    if (help || argc != 7) {
        auto& output = help ? std::cout : std::cerr;
        output << "Usage: " << argv[0]
               << " <server-ipv4> <control-port> <proxy-port>"
                  " <app-ipv4> <app-port> <public-port>\n";
        return help ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    try {
        const auto server_address = boost::asio::ip::make_address_v4(argv[1]);
        Agent::AgentConfig cfg;
        cfg.server_control = {server_address, ParsePort(argv[2])};
        cfg.server_proxy = {server_address, ParsePort(argv[3])};
        cfg.app = {boost::asio::ip::make_address_v4(argv[4]), ParsePort(argv[5])};
        cfg.public_port = ParsePort(argv[6]);

        boost::asio::io_context io;
        auto agent = Agent::Create(io, cfg);
        agent->Start();
        io.run();
        return agent->IsStopped() ? EXIT_FAILURE : EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Agent: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
