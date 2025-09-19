#ifndef BUFFERED_LOGGER_H
#define BUFFERED_LOGGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>
#include <fstream>

namespace DisplayDriver {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

struct LogEntry {
    std::chrono::steady_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::thread::id threadId;
    uint32_t hash;
    size_t count;  // For deduplication tracking
    
    LogEntry() : level(LogLevel::INFO), hash(0), count(1) {}
    LogEntry(LogLevel lvl, const std::string& msg, uint32_t h = 0) 
        : timestamp(std::chrono::steady_clock::now()), 
          level(lvl), 
          message(msg), 
          threadId(std::this_thread::get_id()),
          hash(h),
          count(1) {}
};

class BufferedLogger {
public:
    struct Config {
        size_t bufferSize = 10000;              // Max entries before auto-flush
        size_t maxMemoryBytes = 50 * 1024 * 1024; // 50MB max memory
        std::chrono::milliseconds flushInterval = std::chrono::milliseconds(1000);
        bool enableDeduplication = true;
        size_t deduplicationWindowSize = 1000;  // Last N unique messages to track
        std::chrono::milliseconds deduplicationTimeWindow = std::chrono::milliseconds(5000);
        LogLevel minimumLevel = LogLevel::DEBUG;
        std::string outputFile = "driver.log";
        bool consoleOutput = false;
        bool asyncFlush = true;
    };

    explicit BufferedLogger(const Config& config = Config());
    ~BufferedLogger();

    // Non-copyable, moveable
    BufferedLogger(const BufferedLogger&) = delete;
    BufferedLogger& operator=(const BufferedLogger&) = delete;
    BufferedLogger(BufferedLogger&&) = default;
    BufferedLogger& operator=(BufferedLogger&&) = default;

    // Core logging methods
    void log(LogLevel level, const std::string& message);
    void log(LogLevel level, const char* format, ...);
    
    // Convenience methods
    void trace(const std::string& msg) { log(LogLevel::TRACE, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void critical(const std::string& msg) { log(LogLevel::CRITICAL, msg); }

    // Flush control
    void flush();
    void forceFlush();  // Bypasses async and flushes immediately
    
    // Configuration
    void setMinimumLevel(LogLevel level);
    void enableDeduplication(bool enable);
    void setFlushCallback(std::function<void(const std::vector<LogEntry>&)> callback);
    
    // Statistics
    struct Stats {
        std::atomic<size_t> totalLogged{0};
        std::atomic<size_t> totalFlushed{0};
        std::atomic<size_t> totalDeduplicated{0};
        std::atomic<size_t> currentBufferSize{0};
        std::atomic<size_t> totalFlushes{0};
        std::chrono::steady_clock::time_point lastFlushTime;
    };
    
    const Stats& getStats() const { return m_stats; }
    
    // Shutdown
    void shutdown();

private:
    // Internal methods
    void internalLog(const LogEntry& entry);
    void flushWorker();
    void performFlush();
    uint32_t computeHash(const std::string& message, LogLevel level);
    bool shouldDeduplicate(uint32_t hash, std::chrono::steady_clock::time_point now);
    std::string formatLogEntry(const LogEntry& entry);
    size_t estimateMemoryUsage() const;
    
    // Configuration
    Config m_config;
    
    // Thread safety
    mutable std::mutex m_bufferMutex;
    mutable std::mutex m_flushMutex;
    std::condition_variable m_flushCv;
    std::condition_variable m_shutdownCv;
    
    // Buffers (double buffering for performance)
    std::vector<LogEntry> m_primaryBuffer;
    std::vector<LogEntry> m_secondaryBuffer;
    std::atomic<bool> m_useSecondaryBuffer{false};
    
    // Deduplication
    struct DedupeInfo {
        std::chrono::steady_clock::time_point lastSeen;
        size_t count;
    };
    std::unordered_map<uint32_t, DedupeInfo> m_dedupeMap;
    std::vector<uint32_t> m_dedupeWindow;  // Circular buffer of recent hashes
    size_t m_dedupeWindowIndex = 0;
    
    // Flush thread
    std::unique_ptr<std::thread> m_flushThread;
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_forceFlushRequested{false};
    
    // Output
    std::ofstream m_fileStream;
    std::function<void(const std::vector<LogEntry>&)> m_flushCallback;
    
    // Statistics
    mutable Stats m_stats;
    
    // Performance optimizations
    static thread_local char s_formatBuffer[4096];
};

// Singleton pattern for global logger (common in drivers)
class GlobalLogger {
public:
    static BufferedLogger& getInstance() {
        static BufferedLogger instance;
        return instance;
    }
    
    static void configure(const BufferedLogger::Config& config) {
        getInstance() = BufferedLogger(config);
    }
};

// Convenience macros for driver logging
#define DRIVER_LOG_TRACE(msg) DisplayDriver::GlobalLogger::getInstance().trace(msg)
#define DRIVER_LOG_DEBUG(msg) DisplayDriver::GlobalLogger::getInstance().debug(msg)
#define DRIVER_LOG_INFO(msg) DisplayDriver::GlobalLogger::getInstance().info(msg)
#define DRIVER_LOG_WARNING(msg) DisplayDriver::GlobalLogger::getInstance().warning(msg)
#define DRIVER_LOG_ERROR(msg) DisplayDriver::GlobalLogger::getInstance().error(msg)
#define DRIVER_LOG_CRITICAL(msg) DisplayDriver::GlobalLogger::getInstance().critical(msg)

} // namespace DisplayDriver

#endif // BUFFERED_LOGGER_H