#include "../src/platform.hpp"
#include <iostream>

int main() {
    bool running = kestr::platform::system::is_daemon_running();
    std::cout << "Daemon running: " << (running ? "true" : "false") << std::endl;
    return 0;
}
