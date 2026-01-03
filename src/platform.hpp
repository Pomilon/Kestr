#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <filesystem>

namespace kestr::platform {

    /**
     * @brief Platform-agnostic file system event types.
     */
    struct FileEvent {
        enum class Type {
            Modified,
            Created,
            Deleted,
            Renamed
        };

        std::filesystem::path path;
        Type type;
        std::filesystem::path new_path; // Only for Renamed
    };

    /**
     * @brief Abstract base class for the File Watcher (Sentry).
     * Implementations will use inotify (Linux) or ReadDirectoryChangesW (Windows).
     */
    class Sentry {
    public:
        using EventCallback = std::function<void(const FileEvent&)>;

        virtual ~Sentry() = default;

        /**
         * @brief Starts watching a directory recursively.
         * @param path The root path to watch.
         */
        virtual void add_watch(const std::filesystem::path& path) = 0;

        /**
         * @brief Sets the callback for file events.
         */
        virtual void set_callback(EventCallback callback) = 0;

        /**
         * @brief Starts the watcher loop (non-blocking or threaded).
         */
        virtual void start() = 0;

        /**
         * @brief Stops the watcher.
         */
        virtual void stop() = 0;

        /**
         * @brief Factory method to create a platform-specific Sentry.
         */
        static std::unique_ptr<Sentry> create();
    };

    /**
     * @brief Abstract base class for IPC Server (The Bridge).
     * Implementations will use Unix Domain Sockets (Linux) or Named Pipes (Windows).
     */
    class Bridge {
    public:
        using MessageCallback = std::function<std::string(const std::string&)>;

        virtual ~Bridge() = default;

        /**
         * @brief Initializes the IPC endpoint.
         * @param name The name of the socket/pipe (e.g., "kestr.sock").
         */
        virtual void listen(const std::string& name) = 0;

        /**
         * @brief Sets the handler for incoming messages.
         */
        virtual void set_handler(MessageCallback handler) = 0;

        /**
         * @brief runs the IPC loop.
         */
        virtual void run() = 0;
        
        /**
         * @brief Stops the IPC loop.
         */
        virtual void stop() = 0;

        static std::unique_ptr<Bridge> create();
    };

    /**
     * @brief Abstract base class for IPC Client.
     */
    class Client {
    public:
        virtual ~Client() = default;

        /**
         * @brief Connects to the IPC endpoint.
         * @param name The name of the socket/pipe (e.g., "kestr.sock").
         * @return true if connected successfully.
         */
        virtual bool connect(const std::string& name) = 0;

        /**
         * @brief Sends a message and waits for a response.
         * @param message The message to send.
         * @return The response from the server.
         */
        virtual std::string send(const std::string& message) = 0;

        static std::unique_ptr<Client> create();
    };

    /**
     * @brief System-level helper functions.
     */
    namespace system {
        std::filesystem::path get_config_dir();
        std::filesystem::path get_data_dir();
        bool is_daemon_running();
    }

}
