#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "kestr/types.hpp"

namespace kestr::engine {

    class JobQueue {
    public:
        void push(const FileInfo& info) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(info);
            }
            m_cv.notify_one();
        }

        bool pop(FileInfo& info) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_queue.empty() || m_stop; });
            
            if (m_stop && m_queue.empty()) return false;
            
            info = m_queue.front();
            m_queue.pop();
            return true;
        }

        void stop() {
            m_stop = true;
            m_cv.notify_all();
        }

        size_t size() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

    private:
        std::queue<FileInfo> m_queue;
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_stop{false};
    };

}
