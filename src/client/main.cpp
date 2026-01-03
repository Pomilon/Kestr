#include <iostream>
#include <vector>
#include <string>
#include "platform.hpp"

// Simple JSON helper for avoiding dependency just for this skeleton
std::string make_json_request(const std::string& method, const std::vector<std::string>& params) {
    std::string json = "{\"method\": \"" + method + "\", \"params\": [";
    for (size_t i = 0; i < params.size(); ++i) {
        json += "\"" + params[i] + "\"";
        if (i < params.size() - 1) json += ", ";
    }
    json += "]}";
    return json;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: kestr <command> [args...]\n";
        std::cerr << "Commands:\n";
        std::cerr << "  ping      - Test connection\n";
        std::cerr << "  shutdown  - Stop the daemon\n";
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    auto client = kestr::platform::Client::create();
    if (!client) {
        std::cerr << "Error: Failed to create client platform interface.\n";
        return 1;
    }

    if (!client->connect("kestr.sock")) {
        std::cerr << "Error: Could not connect to kestrd daemon. Is it running?\n";
        return 1;
    }

    std::string request = make_json_request(command, args);
    std::string response = client->send(request);

    std::cout << response << "\n";

    return 0;
}
