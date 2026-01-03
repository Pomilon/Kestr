#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../platform.hpp"

using json = nlohmann::json;

// Helper to send JSON-RPC response
void send_response(const json& id, const json& result) {
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    std::cout << response.dump() << std::endl;
}

void send_error(const json& id, int code, const std::string& message) {
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
    std::cout << response.dump() << std::endl;
}

int main() {
    auto client = kestr::platform::Client::create();
    if (!client || !client->connect("kestr.sock")) {
        std::cerr << "Failed to connect to kestrd daemon.\n";
        return 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            auto req = json::parse(line);
            auto id = req.value("id", json(nullptr));
            std::string method = req.value("method", "");

            // 1. Initialize
            if (method == "initialize") {
                json result = {
                    {"protocolVersion", "0.1.0"},
                    {"capabilities", {
                        {"resources", {}},
                        {"tools", {}}
                    }},
                    {"serverInfo", {
                        {"name", "kestr-mcp"},
                        {"version", "0.1.0"}
                    }}
                };
                send_response(id, result);
                continue;
            }

            // 2. Initialized Notification
            if (method == "notifications/initialized") {
                continue; // No response needed
            }

            // 3. List Tools
            if (method == "tools/list") {
                json result = {
                    {"tools", {
                        {
                            {"name", "kestr_query"},
                            {"description", "Search the indexed codebase for relevant context using semantic and keyword search."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"query", {"type", "string"}, {"description", "The search query."}}
                                }},
                                {"required", {"query"}}
                            }}
                        }
                    }}
                };
                send_response(id, result);
                continue;
            }

            // 4. Call Tool
            if (method == "tools/call") {
                auto params = req.value("params", json::object());
                std::string name = params.value("name", "");
                auto args = params.value("arguments", json::object());

                if (name == "kestr_query") {
                    std::string q = args.value("query", "");
                    
                    // Forward to Daemon via IPC
                    // We reuse the existing simple JSON-RPC of the bridge: {"method": "query", "params": ["q"]}
                    json bridge_req = {
                        {"method", "query"},
                        {"params", {q}}
                    };
                    
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    auto bridge_resp = json::parse(bridge_resp_str);
                    
                    if (bridge_resp.contains("result")) {
                        // Format for MCP: Content list
                        std::string text_content = "Found relevant context:\n\n";
                        for (const auto& item : bridge_resp["result"]) {
                            text_content += "--- File Content ---\n";
                            text_content += item.value("content", "") + "\n";
                        }

                        json result = {
                            {"content", {
                                {
                                    {"type", "text"},
                                    {"text", text_content}
                                }
                            }}
                        };
                        send_response(id, result);
                    } else {
                        send_error(id, -32000, "Daemon error");
                    }
                } else {
                    send_error(id, -32601, "Tool not found");
                }
                continue;
            }

            // Default: Method not found
            // For Ping or other internal methods, we might want to handle them, but for strict MCP, ignore or error.
            // But we need to keep the connection alive if it's a notification.
            if (!req.contains("id")) continue; 
            
            send_error(id, -32601, "Method not found");

        } catch (const std::exception& e) {
            // Malformed JSON or logic error
            // Log to stderr to avoid breaking Stdio transport
            std::cerr << "MCP Error: " << e.what() << "\n";
        }
    }

    return 0;
}
