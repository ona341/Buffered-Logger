#include "buffered_logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdarg>
#include <algorithm>
#include <cstring>

namespace DisplayDriver {

thread_local char BufferedLogger::s_formatBuffer[4096];

BufferedLogger::BufferedLogger(const Config& config) 
    : m_config(config),
      m_primaryBuffer(),
      m_secondaryBuffer() {
    
    // Reserve buffer space
    m_primaryBuffer.reserve(config.bufferSize);
    m_secondaryBuffer.reserve(config.bufferSize);
    
    // Initialize deduplication window
    if (config.enableDeduplication) {
        m_dedupeWindow.resize(config.deduplicationWindowSize, 0);
    }
    
    // Open output file
    if (!config.outputFile.empty()) {
        m_fileStream.open(config.outputFile, std::ios::out | std::ios::app);
        if (!m_fileStream.is_open()) {
            std::cerr << "Failed to open log file: " << config.outputFile << std::endl;
        }
    }
    
    // Start flush thread
    if (config.asyncFlush) {
        m_flushThread = std::make_unique<std::thread>(&BufferedLogger::flushWorker, this);
    }
}

BufferedLogger::~BufferedLogger() {
    shutdown();
}

void BufferedLogger::shutdown() {
    if (m_shutdown.exchange(true)) {
        return; // Already shut down
    }
    
    // Signal shutdown
    {
        std::unique_lock<std::mutex> lock(m_flushMutex);
        m_flushCv.notify_all();
    }
    
    // Wait for flush thread
    if (m_flushThread && m_flushThread->joinable()) {
        m_flushThread->join();
    }
    
    // Final flush
    forceFlush();
    
    // Close file
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

void BufferedLogger::log(LogLevel level, const std::string& message) {
    if (level < m_config.minimumLevel) {
        return;
    }
    
    uint32_t hash = 0;
    if (m_config.enableDeduplication) {
        hash = computeHash(message, level);
        
        auto now = std::chrono::steady_clock::now();
        if (shouldDeduplicate(hash, now)) {
            m_stats.totalDeduplicated.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    
    LogEntry entry(level, message, hash);
    internalLog(entry);
}

void BufferedLogger::log(LogLevel level, const char* format, ...) {
    if (level < m_config.minimumLevel) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    vsnprintf(s_formatBuffer, sizeof(s_formatBuffer), format, args);
    va_end(args);
    
    log(level, std::string(s_formatBuffer));
}

void BufferedLogger::internalLog(const LogEntry& entry) {
    bool shouldFlush = false;
    
    {
        std::unique_lock<std::mutex> lock(m_bufferMutex);
        
        auto& currentBuffer = m_useSecondaryBuffer ? m_secondaryBuffer : m_primaryBuffer;
        currentBuffer.push_back(entry);
        
        m_stats.totalLogged.fetch_add(1, std::memory_order_relaxed);
        m_stats.currentBufferSize.store(currentBuffer.size(), std::memory_order_relaxed);
        
        // Check if we need to flush
        if (currentBuffer.size() >= m_config.bufferSize ||
            estimateMemoryUsage() >= m_config.maxMemoryBytes) {
            shouldFlush = true;
        }
    }
    
    if (shouldFlush) {
        if (m_config.asyncFlush) {
            std::unique_lock<std::mutex> lock(m_flushMutex);
            m_forceFlushRequested = true;
            m_flushCv.notify_one();
        } else {
            performFlush();
        }
    }
}

void BufferedLogger::flush() {
    if (m_config.asyncFlush) {
        std::unique_lock<std::mutex> lock(m_flushMutex);
        m_forceFlushRequested = true;
        m_flushCv.notify_one();
    } else {
        performFlush();
    }
}

void BufferedLogger::forceFlush() {
    performFlush();
}

void BufferedLogger::performFlush() {
    std::vector<LogEntry> bufferToFlush;
    
    {
        std::unique_lock<std::mutex> lock(m_bufferMutex);
        
        auto& currentBuffer = m_useSecondaryBuffer ? m_secondaryBuffer : m_primaryBuffer;
        if (currentBuffer.empty()) {
            return;
        }
        
        // Swap buffers for zero-copy flush
        bufferToFlush.swap(currentBuffer);
        m_useSecondaryBuffer = !m_useSecondaryBuffer;
        
        m_stats.currentBufferSize.store(0, std::memory_order_relaxed);
    }
    
    // Write to file/console outside of lock
    for (const auto& entry : bufferToFlush) {
        std::string formatted = formatLogEntry(entry);
        
        if (m_fileStream.is_open()) {
            m_fileStream << formatted << std::endl;
        }
        
        if (m_config.consoleOutput) {
            std::cout << formatted << std::endl;
        }
    }
    
    if (m_fileStream.is_open()) {
        m_fileStream.flush();
    }
    
    // Call custom flush callback if set
    if (m_flushCallback) {
        m_flushCallback(bufferToFlush);
    }
    
    // Update stats
    m_stats.totalFlushed.fetch_add(bufferToFlush.size(), std::memory_order_relaxed);
    m_stats.totalFlushes.fetch_add(1, std::memory_order_relaxed);
    m_stats.lastFlushTime = std::chrono::steady_clock::now();
    
    m_forceFlushRequested = false;
}

void BufferedLogger::flushWorker() {
    while (!m_shutdown) {
        std::unique_lock<std::mutex> lock(m_flushMutex);
        
        // Wait for flush interval or force flush request
        m_flushCv.wait_for(lock, m_config.flushInterval, [this] {
            return m_shutdown || m_forceFlushRequested;
        });
        
        if (m_shutdown) {
            break;
        }
        
        lock.unlock();
        performFlush();
    }
}

uint32_t BufferedLogger::computeHash(const std::string& message, LogLevel level) {
    // FNV-1a hash for speed
    uint32_t hash = 2166136261u;
    
    // Hash the log level
    hash ^= static_cast<uint32_t>(level);
    hash *= 16777619u;
    
    // Hash the message
    for (char c : message) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }
    
    return hash;
}

bool BufferedLogger::shouldDeduplicate(uint32_t hash, std::chrono::steady_clock::time_point now) {
    std::unique_lock<std::mutex> lock(m_bufferMutex);
    
    auto it = m_dedupeMap.find(hash);
    if (it != m_dedupeMap.end()) {
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.lastSeen);
        
        if (timeDiff < m_config.deduplicationTimeWindow) {
            it->second.count++;
            it->second.lastSeen = now;
            return true; // Deduplicate
        }
    }
    
    // Add to deduplication tracking
    m_dedupeMap[hash] = {now, 1};
    
    // Maintain circular buffer of recent hashes
    if (m_dedupeWindow[m_dedupeWindowIndex] != 0) {
        // Remove old hash from map if it's only referenced here
        auto oldHash = m_dedupeWindow[m_dedupeWindowIndex];
        auto oldIt = m_dedupeMap.find(oldHash);
        if (oldIt != m_dedupeMap.end()) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - oldIt->second.lastSeen);
            if (age >= m_config.deduplicationTimeWindow) {
                m_dedupeMap.erase(oldIt);
            }
        }
    }
    
    m_dedupeWindow[m_dedupeWindowIndex] = hash;
    m_dedupeWindowIndex = (m_dedupeWindowIndex + 1) % m_dedupeWindow.size();
    
    return false; // Don't deduplicate
}

std::string BufferedLogger::formatLogEntry(const LogEntry& entry) {
    static const char* levelStrings[] = {
        "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT "
    };
    
    // Convert timestamp to time_t for formatting
    auto timeT = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + 
        (entry.timestamp - std::chrono::steady_clock::now()));
    
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
    
    // Add milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    
    // Add level
    ss << "[" << levelStrings[static_cast<int>(entry.level)] << "] ";
    
    // Add thread ID
    ss << "[T:" << std::hex << entry.threadId << std::dec << "] ";
    
    // Add message
    ss << entry.message;
    
    // Add deduplication count if applicable
    if (entry.count > 1) {
        ss << " (repeated " << entry.count << " times)";
    }
    
    return ss.str();
}

size_t BufferedLogger::estimateMemoryUsage() const {
    size_t usage = 0;
    
    auto& currentBuffer = m_useSecondaryBuffer ? m_secondaryBuffer : m_primaryBuffer;
    for (const auto& entry : currentBuffer) {
        usage += sizeof(LogEntry) + entry.message.capacity();
    }
    
    return usage;
}

void BufferedLogger::setMinimumLevel(LogLevel level) {
    m_config.minimumLevel = level;
}

void BufferedLogger::enableDeduplication(bool enable) {
    std::unique_lock<std::mutex> lock(m_bufferMutex);
    m_config.enableDeduplication = enable;
    
    if (!enable) {
        m_dedupeMap.clear();
        std::fill(m_dedupeWindow.begin(), m_dedupeWindow.end(), 0);
        m_dedupeWindowIndex = 0;
    }
}

void BufferedLogger::setFlushCallback(std::function<void(const std::vector<LogEntry>&)> callback) {
    m_flushCallback = callback;
}

} // namespace DisplayDriver