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

// Helper to send JSON-RPC error
void send_error(const json& id, int code, const std::string& message) {
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
    std::cout << response.dump() << std::endl;
}

int main() {
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

            auto get_bridge_client = []() {
                auto client = kestr::platform::Client::create();
                if (client && client->connect("kestr.sock")) {
                    return client;
                }
                return std::unique_ptr<kestr::platform::Client>(nullptr);
            };

            // 3. Resources List
            if (method == "resources/list") {
                auto client = get_bridge_client();
                if (!client) {
                    send_error(id, -32001, "Failed to connect to kestrd");
                    continue;
                }
                json bridge_req = {{"method", "resource_list"}, {"params", {}}};
                std::string bridge_resp_str = client->send(bridge_req.dump());
                auto bridge_resp = json::parse(bridge_resp_str);
                
                json resources = json::array();
                if (bridge_resp.contains("result")) {
                    for (const auto& path : bridge_resp["result"]) {
                        resources.push_back({
                            {"uri", "kestr://" + std::string(path)},
                            {"name", path},
                            {"mimeType", "text/plain"} 
                        });
                    }
                }
                send_response(id, {{"resources", resources}});
                continue;
            }

            // 4. Resources Read
            if (method == "resources/read") {
                auto client = get_bridge_client();
                if (!client) {
                    send_error(id, -32001, "Failed to connect to kestrd");
                    continue;
                }
                auto params = req.value("params", json::object());
                std::string uri = params.value("uri", "");
                
                json bridge_req = {{"method", "resource_read"}, {"params", {uri}}};
                std::string bridge_resp_str = client->send(bridge_req.dump());
                auto bridge_resp = json::parse(bridge_resp_str);

                if (bridge_resp.contains("result") && bridge_resp["result"].contains("content")) {
                    send_response(id, {
                        {"contents", {{
                            {"uri", uri},
                            {"mimeType", "text/plain"},
                            {"text", bridge_resp["result"]["content"]}
                        }}}
                    });
                } else {
                    send_error(id, -32002, "Resource not found or error reading");
                }
                continue;
            }

            // 5. List Tools
            if (method == "tools/list") {
                json result = {
                    {"tools", {
                        {
                            {"name", "kestr_query"},
                            {"description", "Search the indexed codebase for relevant context using semantic and keyword search."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"query", {"type", "string"}, {"description", "The search query."}},
                                    {"limit", {"type", "integer"}, {"description", "Max results."}},
                                    {"type_filter", {"type", "string"}, {"description", "Filter by symbol type (e.g., function, class)."}},
                                    {"language", {"type", "string"}, {"description", "Filter by language (e.g., python, cpp)."}},
                                    {"scope", {"type", "string"}, {"description", "Filter by project root path."}}
                                }},
                                {"required", {"query"}}
                            }}
                        },
                        {
                            {"name", "kestr_status"},
                            {"description", "Get the current status of the Kestr daemon (indexing progress, etc.)."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {}},
                                {"required", json::array()}
                            }}
                        }
                    }}
                };
                send_response(id, result);
                continue;
            }

            // 4. Call Tool
            if (method == "tools/call") {
                auto client = get_bridge_client();
                if (!client) {
                    send_error(id, -32001, "Failed to connect to kestrd");
                    continue;
                }
                auto params = req.value("params", json::object());
                std::string name = params.value("name", "");
                auto args = params.value("arguments", json::object());

                if (name == "kestr_status") {
                    json bridge_req = {{"method", "status"}, {"params", json::array()}};
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    if (bridge_resp_str.empty()) {
                        send_error(id, -32000, "Daemon returned empty response");
                        continue;
                    }
                    auto bridge_resp = json::parse(bridge_resp_str);
                    send_response(id, {{"content", {
                        {{"type", "text"}, {"text", bridge_resp["result"].dump()}}
                    }}});
                    continue;
                }

                if (name == "kestr_query") {
                    std::string q = args.value("query", "");
                    int limit = args.value("limit", 5);
                    std::string type_filter = args.value("type_filter", "");
                    std::string language = args.value("language", "");
                    std::string scope = args.value("scope", "");
                    
                    // Forward to Daemon via IPC
                    json bridge_req = {
                        {"method", "query"},
                        {"params", {q, limit, type_filter, language, scope}}
                    };
                    
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    if (bridge_resp_str.empty()) {
                         send_error(id, -32000, "Daemon returned empty response");
                         continue;
                    }
                    auto bridge_resp = json::parse(bridge_resp_str);
                    
                    if (bridge_resp.contains("result")) {
                        // Format for MCP: Content list
                        std::string text_content = "Found relevant context:\n\n";
                        for (const auto& item : bridge_resp["result"]) {
                            text_content += "--- File Content ---\n";
                            text_content += "Symbol: " + item.value("symbol", "N/A") + " (" + item.value("symbol_type", "N/A") + ")\n";
                            text_content += "Lines: " + std::to_string(item["lines"][0].get<int>()) + "-" + std::to_string(item["lines"][1].get<int>()) + "\n";
                            text_content += item.value("content", "") + "\n\n";
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
        } catch (const std::exception& e) {
            // Malformed JSON or logic error
            // Log to stderr to avoid breaking Stdio transport
            std::cerr << "MCP Error: " << e.what() << "\n";
        }
    }

    return 0;
}
