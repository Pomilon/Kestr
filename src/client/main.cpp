#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include "platform.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: kestr <command> [args...]\n";
        std::cerr << "Commands:\n";
        std::cerr << "  ping      - Test connection\n";
        std::cerr << "  status    - Daemon statistics\n";
        std::cerr << "  watch <p> - Add path to watcher\n";
        std::cerr << "  query <q> [limit] - Search for context (default limit 5)\n";
        std::cerr << "  reindex   - Force full re-scan\n";
        std::cerr << "  shutdown  - Stop the daemon\n";
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    std::string method = command;
    if (command == "watch") {
        method = "watch_add";
        if (!args.empty()) {
            try {
                args[0] = std::filesystem::absolute(args[0]).string();
            } catch (...) {}
        }
    }

    // Manual JSON construction to support optional int param for query limit
    std::string json = "{\"method\": \"" + method + "\", \"params\": [";
    for (size_t i = 0; i < args.size(); ++i) {
        if (command == "query" && i == 1) {
             json += args[i]; // Treat second arg of query as int
        } else {
             json += "\"" + args[i] + "\"";
        }
        
        if (i < args.size() - 1) json += ", ";
    }
    json += "]}";

    auto client = kestr::platform::Client::create();
    if (!client) {
        std::cerr << "Error: Failed to create client platform interface.\n";
        return 1;
    }

    if (!client->connect("kestr.sock")) {
        std::cerr << "Error: Could not connect to kestrd daemon. Is it running?\n";
        return 1;
    }

    std::string response = client->send(json);
    std::cout << response << "\n";

    return 0;
}
