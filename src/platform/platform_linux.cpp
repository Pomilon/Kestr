#include "../platform.hpp"
#include <iostream>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <map>
#include <vector>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>

namespace kestr::platform {

    class LinuxSentry : public Sentry {
    public:
        LinuxSentry() {
            m_fd = inotify_init1(IN_NONBLOCK);
            if (m_fd < 0) {
                std::cerr << "[LinuxSentry] Failed to initialize inotify.\n";
            }
        }

        ~LinuxSentry() {
            stop();
            if (m_fd >= 0) close(m_fd);
        }

        void add_watch(const std::filesystem::path& path) override {
            if (m_fd < 0) return;
            
            // Recursively add watches
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                add_watch_single(path);
                for (auto const& dir_entry : std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied)) {
                    if (dir_entry.is_directory()) {
                        add_watch_single(dir_entry.path());
                    }
                }
            } else {
                // For individual files, we usually watch the parent dir, but here we assume path is a root dir to watch
            }
        }

        void set_callback(EventCallback callback) override {
            m_callback = callback;
        }

        void start() override {
            if (m_fd < 0) return;
            m_running = true;
            std::cout << "[LinuxSentry] Starting watcher loop...\n";
            
            struct pollfd pfd = { m_fd, POLLIN, 0 };
            char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));

            while (m_running) {
                int poll_num = poll(&pfd, 1, 500); // 500ms timeout
                if (poll_num > 0) {
                    if (pfd.revents & POLLIN) {
                        ssize_t len = read(m_fd, buffer, sizeof(buffer));
                        if (len < 0 && errno != EAGAIN) {
                            std::cerr << "[LinuxSentry] read error\n";
                        }
                        
                        const struct inotify_event *event;
                        for (char *ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
                            event = (const struct inotify_event *) ptr;
                            handle_event(event);
                        }
                    }
                }
            }
        }

        void stop() override {
            m_running = false;
        }

    private:
        int m_fd = -1;
        std::atomic<bool> m_running{false};
        std::map<int, std::filesystem::path> m_watches; // wd -> path
        EventCallback m_callback;

        void add_watch_single(const std::filesystem::path& path) {
            int wd = inotify_add_watch(m_fd, path.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
            if (wd >= 0) {
                m_watches[wd] = path;
                // std::cout << "[LinuxSentry] Watching: " << path << "\n";
            } else {
                // std::cerr << "[LinuxSentry] Failed to watch: " << path << "\n";
            }
        }

        void handle_event(const struct inotify_event* event) {
            if (event->mask & IN_Q_OVERFLOW) {
                std::cerr << "[LinuxSentry] Event queue overflow.\n";
                return;
            }

            if (m_watches.find(event->wd) == m_watches.end()) return;

            std::filesystem::path parent = m_watches[event->wd];
            std::filesystem::path full_path = parent / event->name;

            // Simple Logic: just map inotify mask to our enum
            // Also handle new directories
            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                add_watch_single(full_path);
            }

            if (!m_callback) return;

            FileEvent fe;
            fe.path = full_path;
            
            if (event->mask & IN_CREATE) fe.type = FileEvent::Type::Created;
            else if (event->mask & IN_DELETE) fe.type = FileEvent::Type::Deleted;
            else if (event->mask & IN_MODIFY) fe.type = FileEvent::Type::Modified;
            else if (event->mask & IN_MOVED_FROM) fe.type = FileEvent::Type::Renamed; // Incomplete, needs cookie tracking for TO
            else if (event->mask & IN_MOVED_TO) {
                // Treated as Created for now or needs cookie logic
                fe.type = FileEvent::Type::Created; 
            }
            else return;

            // Filter out purely directory events if needed, but we usually want to know
            m_callback(fe);
        }
    };

    class LinuxBridge : public Bridge {
    public:
        LinuxBridge() = default;
        ~LinuxBridge() { stop(); }

        void listen(const std::string& name) override {
            m_socket_path = "/tmp/" + name;
            unlink(m_socket_path.c_str());

            m_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (m_server_fd < 0) {
                std::cerr << "[LinuxBridge] Failed to create socket.\n";
                return;
            }

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

            if (bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                std::cerr << "[LinuxBridge] Failed to bind socket: " << strerror(errno) << "\n";
                close(m_server_fd);
                m_server_fd = -1;
                return;
            }

            if (::listen(m_server_fd, 5) < 0) {
                std::cerr << "[LinuxBridge] Failed to listen on socket.\n";
                close(m_server_fd);
                m_server_fd = -1;
                return;
            }

            std::cout << "[LinuxBridge] Listening on " << m_socket_path << "\n";
        }

        void set_handler(MessageCallback handler) override {
            m_handler = handler;
        }

        void run() override {
            if (m_server_fd < 0) return;
            m_running = true;

            struct pollfd pfd = { m_server_fd, POLLIN, 0 };

            while (m_running) {
                int poll_num = poll(&pfd, 1, 500);
                if (poll_num > 0 && (pfd.revents & POLLIN)) {
                    int client_fd = accept(m_server_fd, nullptr, nullptr);
                    if (client_fd >= 0) {
                        handle_client(client_fd);
                    }
                }
            }
        }

        void stop() override {
            m_running = false;
            if (m_server_fd >= 0) {
                close(m_server_fd);
                unlink(m_socket_path.c_str());
                m_server_fd = -1;
            }
        }

    private:
        int m_server_fd = -1;
        std::string m_socket_path;
        MessageCallback m_handler;
        std::atomic<bool> m_running{false};

        void handle_client(int client_fd) {
            char buffer[4096];
            ssize_t len = read(client_fd, buffer, sizeof(buffer) - 1);
            if (len > 0) {
                buffer[len] = '\0';
                std::string request(buffer);
                std::string response = "{}";
                if (m_handler) {
                    response = m_handler(request);
                }
                write(client_fd, response.c_str(), response.size());
            }
            close(client_fd);
        }
    };

    class LinuxClient : public Client {
    public:
        bool connect(const std::string& name) override {
            m_socket_path = "/tmp/" + name;
            m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (m_fd < 0) return false;

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

            if (::connect(m_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(m_fd);
                m_fd = -1;
                return false;
            }
            return true;
        }

        std::string send(const std::string& message) override {
            if (m_fd < 0) return "";
            if (write(m_fd, message.c_str(), message.size()) < 0) return "";

            char buffer[4096];
            ssize_t len = read(m_fd, buffer, sizeof(buffer) - 1);
            if (len >= 0) {
                buffer[len] = '\0';
                return std::string(buffer);
            }
            return "";
        }
        
        ~LinuxClient() {
            if (m_fd >= 0) close(m_fd);
        }

    private:
        int m_fd = -1;
        std::string m_socket_path;
    };

    // Factory implementations
    std::unique_ptr<Sentry> Sentry::create() {
        return std::make_unique<LinuxSentry>();
    }

    std::unique_ptr<Bridge> Bridge::create() {
        return std::make_unique<LinuxBridge>();
    }

    std::unique_ptr<Client> Client::create() {
        return std::make_unique<LinuxClient>();
    }

    namespace system {
        std::filesystem::path get_config_dir() {
            const char* home = std::getenv("HOME");
            return home ? std::filesystem::path(home) / ".config/kestr" : "";
        }
        std::filesystem::path get_data_dir() {
            const char* home = std::getenv("HOME");
            return home ? std::filesystem::path(home) / ".local/share/kestr" : "";
        }
        bool is_daemon_running() {
            return false;
        }
    }

}