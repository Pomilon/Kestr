#include "platform.hpp"
#include <windows.h>
#include <io.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <map>

namespace kestr::platform {

    class WindowsSentry : public Sentry {
        EventCallback m_callback;
        std::atomic<bool> m_running{false};
        std::thread m_worker;
        std::map<std::filesystem::path, HANDLE> m_handles;

        void watch_loop(const std::filesystem::path& root, HANDLE hDir) {
            BYTE buffer[1024 * 16];
            DWORD bytesReturned;
            
            while (m_running) {
                if (ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), TRUE, 
                                         FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | 
                                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION, 
                                         &bytesReturned, NULL, NULL)) {
                    
                    FILE_NOTIFY_INFORMATION* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
                    do {
                        std::wstring wpath(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                        std::filesystem::path full_path = root / wpath;
                        
                        FileEvent event;
                        event.path = full_path;

                        switch (fni->Action) {
                            case FILE_ACTION_ADDED: event.type = FileEvent::Type::Created; break;
                            case FILE_ACTION_REMOVED: event.type = FileEvent::Type::Deleted; break;
                            case FILE_ACTION_MODIFIED: event.type = FileEvent::Type::Modified; break;
                            case FILE_ACTION_RENAMED_OLD_NAME: event.type = FileEvent::Type::Renamed; break;
                            case FILE_ACTION_RENAMED_NEW_NAME: 
                                event.type = FileEvent::Type::Created; 
                                break;
                            default: event.type = FileEvent::Type::Modified; break;
                        }

                        if (m_callback) m_callback(event);
                        
                        if (fni->NextEntryOffset == 0) break;
                        fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                            reinterpret_cast<BYTE*>(fni) + fni->NextEntryOffset);
                    } while (true);
                }
            }
        }

    public:
        void add_watch(const std::filesystem::path& path) override {
            HANDLE hDir = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY, 
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (hDir == INVALID_HANDLE_VALUE) return;
            
            m_handles[path] = hDir;
            if (m_running) {
                m_worker = std::thread(&WindowsSentry::watch_loop, this, path, hDir);
                m_worker.detach();
            }
        }

        void set_callback(EventCallback callback) override { m_callback = callback; }

        void start() override {
            m_running = true;
            for (auto const& [path, handle] : m_handles) {
                m_worker = std::thread(&WindowsSentry::watch_loop, this, path, handle);
                m_worker.detach();
            }
        }

        void stop() override {
            m_running = false;
            for (auto const& [path, handle] : m_handles) {
                CloseHandle(handle);
            }
            m_handles.clear();
        }
    };

    class WindowsBridge : public Bridge {
        std::string m_pipe_name;
        MessageCallback m_handler;
        std::atomic<bool> m_running{false};
        std::thread m_worker;

    public:
        void listen(const std::string& name) override {
            if (name.find('\\') != std::string::npos) {
                m_pipe_name = name;
            } else {
                m_pipe_name = "\\\\.\\pipe\\" + name;
            }
        }

        void set_handler(MessageCallback handler) override { m_handler = handler; }

        void run() override {
            m_running = true;
            m_worker = std::thread([this]() {
                while (m_running) {
                    HANDLE hPipe = CreateNamedPipeA(m_pipe_name.c_str(), 
                                                   PIPE_ACCESS_DUPLEX, 
                                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 
                                                   PIPE_UNLIMITED_INSTANCES, 8192, 8192, 0, NULL);
                    if (hPipe == INVALID_HANDLE_VALUE) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }

                    if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
                        char buffer[8192];
                        DWORD bytesRead;
                        if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                            buffer[bytesRead] = '\0';
                            if (m_handler) {
                                std::string response = m_handler(std::string(buffer));
                                DWORD bytesWritten;
                                WriteFile(hPipe, response.c_str(), (DWORD)response.size(), &bytesWritten, NULL);
                            }
                        }
                    }
                    DisconnectNamedPipe(hPipe);
                    CloseHandle(hPipe);
                }
            });
        }

        void stop() override {
            m_running = false;
            // To break ConnectNamedPipe, we can connect a dummy client
            HANDLE hPipe = CreateFileA(m_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
            
            if (m_worker.joinable()) m_worker.join();
        }
    };

    class WindowsClient : public Client {
        HANDLE m_hPipe = INVALID_HANDLE_VALUE;

    public:
        bool connect(const std::string& name) override {
            std::string pipe_path;
            if (name.find('\\') != std::string::npos) {
                pipe_path = name;
            } else {
                pipe_path = "\\\\.\\pipe\\" + name;
            }
            
            m_hPipe = CreateFileA(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 
                                  0, NULL, OPEN_EXISTING, 0, NULL);
            return m_hPipe != INVALID_HANDLE_VALUE;
        }

        std::string send(const std::string& message) override {
            if (m_hPipe == INVALID_HANDLE_VALUE) return "";
            DWORD bytesWritten;
            WriteFile(m_hPipe, message.c_str(), (DWORD)message.size(), &bytesWritten, NULL);
            
            char buffer[8192];
            DWORD bytesRead;
            if (ReadFile(m_hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                return std::string(buffer);
            }
            return "";
        }
        
        ~WindowsClient() {
            if (m_hPipe != INVALID_HANDLE_VALUE) CloseHandle(m_hPipe);
        }
    };

    std::unique_ptr<Sentry> Sentry::create() { return std::make_unique<WindowsSentry>(); }
    std::unique_ptr<Bridge> Bridge::create() { return std::make_unique<WindowsBridge>(); }
    std::unique_ptr<Client> Client::create() { return std::make_unique<WindowsClient>(); }

    namespace system {
        std::filesystem::path get_config_dir() {
            const char* appdata = std::getenv("APPDATA");
            if (!appdata) return "";
            return std::filesystem::path(appdata) / "kestr";
        }
        std::filesystem::path get_data_dir() {
            const char* localappdata = std::getenv("LOCALAPPDATA");
            if (!localappdata) return "";
            return std::filesystem::path(localappdata) / "kestr";
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
            return _isatty(_fileno(stdin));
        }
    }
}
