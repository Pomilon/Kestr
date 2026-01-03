#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <filesystem>

#include "platform.hpp"
#include "engine/scanner.hpp"

// Global stop signal
std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\n[Kestr] Interrupt signal (" << signum << ") received. Shutting down...\n";
    g_running = false;
}

int main(int argc, char* argv[]) {
    // 1. Setup Signal Handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[Kestr] Starting daemon (v0.1.0)...\n";

    // DEMO: Run Scanner
    std::cout << "[Kestr] Running initial scan on current directory...\n";
    kestr::engine::Scanner scanner;
    scanner.scan(std::filesystem::current_path(), [](const kestr::engine::FileInfo& info) {
        std::cout << "[Scanner] Found: " << info.path.filename() 
                  << " (" << info.size << " bytes) Hash: " << info.hash.substr(0, 8) << "...\n";
    });
    std::cout << "[Kestr] Scan complete.\n";


    // 2. Initialize Platform Components
    // Sentry (File Watcher)
    auto sentry = kestr::platform::Sentry::create();
    if (!sentry) {
        std::cerr << "[Kestr] Failed to initialize Sentry (File Watcher).\n";
        return 1;
    }

    // Bridge (IPC)
    auto bridge = kestr::platform::Bridge::create();
    if (!bridge) {
        std::cerr << "[Kestr] Failed to initialize Bridge (IPC).\n";
        return 1;
    }

    // 3. Configure Components
    
    bridge->set_handler([](const std::string& request) -> std::string {
        // Very basic manual JSON parsing for demo
        if (request.find("\"method\": \"ping\"") != std::string::npos) {
            return "{\"result\": \"pong\"}";
        }
        if (request.find("\"method\": \"shutdown\"") != std::string::npos) {
            g_running = false;
            return "{\"result\": \"shutting down\"}";
        }
        return "{\"error\": \"unknown method\"}";
    });

    sentry->set_callback([](const kestr::platform::FileEvent& event) {
        std::string type;
        switch(event.type) {
            case kestr::platform::FileEvent::Type::Modified: type = "MODIFIED"; break;
            case kestr::platform::FileEvent::Type::Created:  type = "CREATED "; break;
            case kestr::platform::FileEvent::Type::Deleted:  type = "DELETED "; break;
            case kestr::platform::FileEvent::Type::Renamed:  type = "RENAMED "; break;
        }
        std::cout << "[Sentry] " << type << ": " << event.path << "\n";
    });

    std::cout << "[Kestr] Watching current directory...\n";
    sentry->add_watch(std::filesystem::current_path());

    // 4. Start Loops
    std::cout << "[Kestr] Components initialized. Entering main loop.\n";
    
    std::thread bridge_thread([&bridge]() {
        bridge->listen("kestr.sock");
        bridge->run();
    });

    std::thread sentry_thread([&sentry]() {
        sentry->start();
    });

    // 5. Main Lifecycle Loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 6. Cleanup
    std::cout << "[Kestr] Stopping components...\n";
    sentry->stop();
    bridge->stop();
    
    // Inotify poll might block, so join might hang if we don't wake it up.
    // Ideally we write to a pipe to wake up poll, or have a timeout.
    // This implementation uses 500ms timeout in poll, so it should exit.

    if (bridge_thread.joinable()) bridge_thread.detach(); // Detach for now as it's a stub loop
    if (sentry_thread.joinable()) sentry_thread.join();

    std::cout << "[Kestr] Shutdown complete. Goodbye.\n";

    return 0;
}
