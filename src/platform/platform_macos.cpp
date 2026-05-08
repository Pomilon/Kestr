#include "platform.hpp"
#include <CoreServices/CoreServices.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>

namespace kestr::platform {

    static std::filesystem::path get_socket_full_path(const std::string& name) {
        std::filesystem::path p;
        if (name.find('/') != std::string::npos) {
            p = std::filesystem::path(name);
        } else {
            const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
            if (runtime_dir) {
                p = std::filesystem::path(runtime_dir) / "kestr" / name;
            } else {
                p = std::filesystem::temp_directory_path() / name;
            }
        }
        if (!p.parent_path().empty()) {
            std::filesystem::create_directories(p.parent_path());
        }
        return p;
    }

    class MacOSSentry : public Sentry {
        EventCallback m_callback;
        std::atomic<bool> m_running{false};
        std::map<std::filesystem::path, FSEventStreamRef> m_watches;
        std::mutex m_mutex;
        CFRunLoopRef m_runLoop = nullptr;
        std::thread m_thread;

        static void event_callback(ConstFSEventStreamRef streamRef, void* clientCallBackInfo, 
                                  size_t numEvents, void* eventPaths, 
                                  const FSEventStreamEventFlags eventFlags[], 
                                  const FSEventStreamEventId eventIds[]) {
            auto* self = static_cast<MacOSSentry*>(clientCallBackInfo);
            if (!self->m_callback) return;

            const char** paths = (const char**)eventPaths;
            for (size_t i = 0; i < numEvents; ++i) {
                FileEvent event;
                event.path = paths[i];
                
                if (eventFlags[i] & kFSEventStreamEventFlagItemRemoved) event.type = FileEvent::Type::Deleted;
                else if (eventFlags[i] & kFSEventStreamEventFlagItemCreated) event.type = FileEvent::Type::Created;
                else if (eventFlags[i] & kFSEventStreamEventFlagItemRenamed) event.type = FileEvent::Type::Renamed;
                else event.type = FileEvent::Type::Modified;

                self->m_callback(event);
            }
        }

    public:
        void add_watch(const std::filesystem::path& path) override {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            CFStringRef pathRef = CFStringCreateWithCString(NULL, path.c_str(), kCFStringEncodingUTF8);
            CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void**)&pathRef, 1, NULL);
            
            FSEventStreamContext context = {0, this, NULL, NULL, NULL};
            FSEventStreamRef stream = FSEventStreamCreate(NULL, &event_callback, &context, pathsToWatch, 
                                                         kFSEventStreamEventIdSinceNow, 1.0, 
                                                         kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
            
            if (stream) {
                m_watches[path] = stream;
                if (m_runLoop) {
                    FSEventStreamScheduleWithRunLoop(stream, m_runLoop, kCFRunLoopDefaultMode);
                    FSEventStreamStart(stream);
                }
            }
            
            CFRelease(pathsToWatch);
            CFRelease(pathRef);
        }

        void set_callback(EventCallback callback) override { m_callback = callback; }

        void start() override {
            m_running = true;
            m_thread = std::thread([this]() {
                m_runLoop = CFRunLoopGetCurrent();
                
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (auto const& [path, stream] : m_watches) {
                        FSEventStreamScheduleWithRunLoop(stream, m_runLoop, kCFRunLoopDefaultMode);
                        FSEventStreamStart(stream);
                    }
                }
                
                CFRunLoopRun();
            });
        }

        void stop() override {
            m_running = false;
            if (m_runLoop) {
                CFRunLoopStop(m_runLoop);
            }
            if (m_thread.joinable()) m_thread.join();

            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto const& [path, stream] : m_watches) {
                FSEventStreamStop(stream);
                FSEventStreamInvalidate(stream);
                FSEventStreamRelease(stream);
            }
            m_watches.clear();
        }
    };

    class MacOSBridge : public Bridge {
        int m_server_fd = -1;
        std::string m_socket_path;
        MessageCallback m_handler;
        std::atomic<bool> m_running{false};
        std::thread m_thread;

    public:
        void listen(const std::string& name) override {
            m_socket_path = get_socket_full_path(name).string();
            unlink(m_socket_path.c_str());

            m_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (m_server_fd < 0) return;

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

            if (bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(m_server_fd);
                m_server_fd = -1;
                return;
            }

            if (::listen(m_server_fd, 5) < 0) {
                close(m_server_fd);
                m_server_fd = -1;
                return;
            }
        }

        void set_handler(MessageCallback handler) override { m_handler = handler; }

        void run() override {
            if (m_server_fd < 0) return;
            m_running = true;
            m_thread = std::thread([this]() {
                struct pollfd pfd = { m_server_fd, POLLIN, 0 };

                while (m_running) {
                    int poll_num = poll(&pfd, 1, 100);
                    if (poll_num > 0 && (pfd.revents & POLLIN)) {
                        int client_fd = accept(m_server_fd, nullptr, nullptr);
                        if (client_fd >= 0) {
                            handle_client(client_fd);
                        }
                    }
                }
            });
        }

        void stop() override {
            m_running = false;
            if (m_thread.joinable()) m_thread.join();
            if (m_server_fd >= 0) {
                close(m_server_fd);
                if (!m_socket_path.empty()) {
                    unlink(m_socket_path.c_str());
                }
                m_server_fd = -1;
            }
        }

    private:
        void handle_client(int client_fd) {
            char buffer[8192];
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

    class MacOSClient : public Client {
        int m_fd = -1;
    public:
        bool connect(const std::string& name) override {
            std::string socket_path = get_socket_full_path(name).string();
            m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (m_fd < 0) return false;

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

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

            std::string response;
            char buffer[8192];
            ssize_t len;
            while ((len = read(m_fd, buffer, sizeof(buffer))) > 0) {
                response.append(buffer, len);
            }
            return response;
        }

        ~MacOSClient() {
            if (m_fd >= 0) close(m_fd);
        }
    };

    std::unique_ptr<Sentry> Sentry::create() { return std::make_unique<MacOSSentry>(); }
    std::unique_ptr<Bridge> Bridge::create() { return std::make_unique<MacOSBridge>(); }
    std::unique_ptr<Client> Client::create() { return std::make_unique<MacOSClient>(); }

    namespace system {
        std::filesystem::path get_config_dir() {
            const char* home = std::getenv("HOME");
            return home ? (std::filesystem::path(home) / ".config/kestr") : "";
        }
        std::filesystem::path get_data_dir() {
            const char* home = std::getenv("HOME");
            return home ? (std::filesystem::path(home) / ".local/share/kestr") : "";
        }
        bool is_daemon_running() {
            auto client = Client::create();
            if (client->connect("kestr.sock")) {
                std::string pong = client->send("{\"method\":\"ping\"}");
                return pong.find("pong") != std::string::npos;
            }
            return false;
        }
        bool is_terminal() {
            return isatty(fileno(stdin));
        }
    }
}
