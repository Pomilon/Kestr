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
    if (kestr::platform::system::is_terminal()) {
        std::cerr << "Kestr MCP Server (v0.2.0)\n";
        std::cerr << "This server implements the Model Context Protocol (MCP) over stdio.\n";
        std::cerr << "It is intended to be run by an MCP client (e.g. Claude Desktop, mcp-inspector).\n";
        std::cerr << "To test manually, pipe a JSON-RPC request to stdin, for example:\n";
        std::cerr << "  echo '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}' | kestr-mcp\n\n";
        std::cerr << "Waiting for JSON-RPC input...\n";
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
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {
                        {"resources", json::object()},
                        {"tools", json::object()}
                    }},
                    {"serverInfo", {
                        {"name", "kestr-mcp"},
                        {"version", "0.2.0"}
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
                                    {"query", {{"type", "string"}, {"description", "The search query."}}},
                                    {"limit", {{"type", "integer"}, {"description", "Max results."}}},
                                    {"type_filter", {{"type", "string"}, {"description", "Filter by symbol type (e.g., function, class)."}}},
                                    {"language", {{"type", "string"}, {"description", "Filter by language (e.g., python, cpp)."}}},
                                    {"scope", {{"type", "string"}, {"description", "Filter by project root path."}}}
                                }},
                                {"required", {"query"}}
                            }}
                        },
                        {
                            {"name", "kestr_status"},
                            {"description", "Get the current status of the Kestr daemon (indexing progress, etc.)."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()},
                                {"required", json::array()}
                            }}
                        },
                        {
                            {"name", "kestr_summarize"},
                            {"description", "Recursively list all files and sizes in a project directory."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"path", {{"type", "string"}, {"description", "The root directory to summarize."}}}
                                }},
                                {"required", {"path"}}
                            }}
                        },
                        {
                            {"name", "kestr_find_references"},
                            {"description", "Find all code snippets that reference a specific symbol."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"symbol", {{"type", "string"}, {"description", "The symbol name to search for."}}}
                                }},
                                {"required", {"symbol"}}
                            }}
                        },
                        {
                            {"name", "kestr_get_definition"},
                            {"description", "Get the definition of a specific class or function."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"symbol", {{"type", "string"}, {"description", "The symbol name."}}}
                                }},
                                {"required", {"symbol"}}
                            }}
                        },
                        {
                            {"name", "kestr_list_symbols"},
                            {"description", "List all symbols (functions, classes) found in a specific file."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"path", {{"type", "string"}, {"description", "The file path."}}}
                                }},
                                {"required", {"path"}}
                            }}
                        },
                        {
                            {"name", "kestr_watch_add"},
                            {"description", "Add a new directory to the Kestr file watcher and index it."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"path", {{"type", "string"}, {"description", "The directory path to watch."}}}
                                }},
                                {"required", {"path"}}
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
                    std::string text_status = "Kestr Status:\n";
                    if (bridge_resp.contains("result")) {
                        for (auto it = bridge_resp["result"].begin(); it != bridge_resp["result"].end(); ++it) {
                            text_status += it.key() + ": " + it.value().dump() + "\n";
                        }
                    }
                    send_response(id, {{"content", {
                        {{"type", "text"}, {"text", text_status}}
                    }}});
                    continue;
                }

                if (name == "kestr_summarize") {
                    std::string path = args.value("path", "");
                    json bridge_req = {{"method", "summarize_project"}, {"params", {path}}};
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    auto bridge_resp = json::parse(bridge_resp_str);
                    
                    if (bridge_resp.contains("result")) {
                        std::string summary = "Project Summary for " + path + ":\n\n";
                        for (const auto& item : bridge_resp["result"]) {
                            summary += "- " + item.value("path", "") + " (" + std::to_string(item.value("size", 0)) + " bytes)\n";
                        }
                        send_response(id, {{"content", {{{"type", "text"}, {"text", summary}}}}});
                    } else {
                        send_error(id, -32000, "Daemon error");
                    }
                    continue;
                }

                if (name == "kestr_find_references") {
                    std::string symbol = args.value("symbol", "");
                    json bridge_req = {{"method", "find_references"}, {"params", {symbol}}};
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    auto bridge_resp = json::parse(bridge_resp_str);

                    if (bridge_resp.contains("result")) {
                        std::string text = "References found for '" + symbol + "':\n\n";
                        for (const auto& item : bridge_resp["result"]) {
                            text += "--- " + item.value("symbol", "N/A") + " ---\n";
                            text += item.value("content", "") + "\n\n";
                        }
                        send_response(id, {{"content", {{{"type", "text"}, {"text", text}}}}});
                    } else {
                        send_error(id, -32000, "Daemon error");
                    }
                    continue;
                }

                if (name == "kestr_get_definition") {
                    std::string symbol = args.value("symbol", "");
                    json bridge_req = {{"method", "get_definition"}, {"params", {symbol}}};
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    auto bridge_resp = json::parse(bridge_resp_str);

                    if (bridge_resp.contains("result")) {
                        auto res = bridge_resp["result"];
                        std::string text = "Definition for '" + symbol + "':\n\n";
                        text += res.value("content", "");
                        send_response(id, {{"content", {{{"type", "text"}, {"text", text}}}}});
                    } else {
                        send_error(id, -32000, "Definition not found");
                    }
                    continue;
                }

                if (name == "kestr_list_symbols") {
                    std::string path = args.value("path", "");
                    json bridge_req = {{"method", "list_symbols"}, {"params", {path}}};
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    auto bridge_resp = json::parse(bridge_resp_str);

                    if (bridge_resp.contains("result")) {
                        std::string text = "Symbols in " + path + ":\n\n";
                        for (const auto& s : bridge_resp["result"]) {
                            text += "- " + s.value("name", "") + " (" + s.value("type", "") + ") @ line " + std::to_string(s.value("line", 0)) + "\n";
                        }
                        send_response(id, {{"content", {{{"type", "text"}, {"text", text}}}}});
                    } else {
                        send_error(id, -32000, "File not found or no symbols");
                    }
                    continue;
                }

                if (name == "kestr_watch_add") {
                    std::string path = args.value("path", "");
                    json bridge_req = {{"method", "watch_add"}, {"params", {path}}};
                    std::string bridge_resp_str = client->send(bridge_req.dump());
                    auto bridge_resp = json::parse(bridge_resp_str);

                    if (bridge_resp.contains("result")) {
                        send_response(id, {{"content", {{{"type", "text"}, {"text", bridge_resp["result"].get<std::string>()}}}}});
                    } else {
                        send_error(id, -32000, bridge_resp.value("error", "Unknown error"));
                    }
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
