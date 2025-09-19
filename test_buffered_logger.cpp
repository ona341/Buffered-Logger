#include "buffered_logger.h"
#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>
#include <fstream>
#include <iomanip>

using namespace DisplayDriver;
using namespace std::chrono_literals;

class TestHarness {
private:
    int m_totalTests = 0;
    int m_passedTests = 0;
    std::string m_currentTest;
    
public:
    void startTest(const std::string& name) {
        m_currentTest = name;
        std::cout << "\n[TEST] " << name << "..." << std::flush;
        m_totalTests++;
    }
    
    void endTest(bool passed, const std::string& message = "") {
        if (passed) {
            std::cout << " PASSED";
            m_passedTests++;
        } else {
            std::cout << " FAILED";
            if (!message.empty()) {
                std::cout << " - " << message;
            }
        }
        std::cout << std::endl;
    }
    
    void assertCondition(bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error("Assertion failed: " + message);
        }
    }
    
    void printSummary() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Summary: " << m_passedTests << "/" << m_totalTests 
                  << " tests passed" << std::endl;
        std::cout << "========================================" << std::endl;
    }
};

// Test 1: Basic Logging
void testBasicLogging(TestHarness& harness) {
    harness.startTest("Basic Logging");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_basic.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        logger.trace("Trace message");
        logger.debug("Debug message");
        logger.info("Info message");
        logger.warning("Warning message");
        logger.error("Error message");
        logger.critical("Critical message");
        
        logger.forceFlush();
        
        // Verify stats
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged >= 5, "Should have logged at least 5 messages");
        
        // Verify file output
        std::ifstream file(config.outputFile);
        harness.assertCondition(file.is_open(), "Log file should exist");
        
        int lineCount = 0;
        std::string line;
        while (std::getline(file, line)) {
            lineCount++;
        }
        harness.assertCondition(lineCount >= 5, "File should contain at least 5 lines");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 2: Deduplication
void testDeduplication(TestHarness& harness) {
    harness.startTest("Deduplication");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_dedup.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        config.enableDeduplication = true;
        config.deduplicationTimeWindow = 100ms;
        
        BufferedLogger logger(config);
        
        // Log same message multiple times quickly
        for (int i = 0; i < 10; i++) {
            logger.info("Duplicate message");
        }
        
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalDeduplicated > 0, "Should have deduplicated messages");
        harness.assertCondition(stats.totalLogged < 10, "Should have logged fewer than 10 messages");
        
        // Wait for dedup window to expire
        std::this_thread::sleep_for(150ms);
        
        // Log same message again - should not be deduplicated
        logger.info("Duplicate message");
        logger.forceFlush();
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 3: Thread Safety
void testThreadSafety(TestHarness& harness) {
    harness.startTest("Thread Safety");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_threads.log";
        config.consoleOutput = false;
        config.bufferSize = 100;
        config.asyncFlush = true;
        
        BufferedLogger logger(config);
        
        std::atomic<int> totalLogged{0};
        const int numThreads = 10;
        const int logsPerThread = 100;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < numThreads; t++) {
            threads.emplace_back([&logger, &totalLogged, t, logsPerThread]() {
                for (int i = 0; i < logsPerThread; i++) {
                    std::stringstream msg;
                    msg << "Thread " << t << " message " << i;
                    logger.info(msg.str());
                    totalLogged.fetch_add(1);
                    
                    // Random small delay
                    std::this_thread::sleep_for(std::chrono::microseconds(rand() % 100));
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Force final flush
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged == totalLogged, 
                              "All messages should be logged");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 4: Buffer Overflow and Auto-Flush
void testBufferOverflow(TestHarness& harness) {
    harness.startTest("Buffer Overflow and Auto-Flush");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_overflow.log";
        config.consoleOutput = false;
        config.bufferSize = 10;  // Small buffer
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        // Log more than buffer size
        for (int i = 0; i < 25; i++) {
            logger.info("Message " + std::to_string(i));
        }
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalFlushes > 0, "Should have auto-flushed");
        
        logger.forceFlush();
        
        harness.assertCondition(stats.totalLogged == 25, "All messages should be logged");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 5: Memory Limit
void testMemoryLimit(TestHarness& harness) {
    harness.startTest("Memory Limit");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_memory.log";
        config.consoleOutput = false;
        config.maxMemoryBytes = 1024;  // 1KB limit
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        // Create large messages
        std::string largeMessage(200, 'X');  // 200 byte message
        
        for (int i = 0; i < 10; i++) {
            logger.info(largeMessage);
        }
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalFlushes > 0, "Should have flushed due to memory limit");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 6: Log Level Filtering
void testLogLevelFiltering(TestHarness& harness) {
    harness.startTest("Log Level Filtering");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_levels.log";
        config.consoleOutput = false;
        config.minimumLevel = LogLevel::WARNING;
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        logger.trace("Should not appear");
        logger.debug("Should not appear");
        logger.info("Should not appear");
        logger.warning("Should appear");
        logger.error("Should appear");
        logger.critical("Should appear");
        
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged == 3, "Only WARNING and above should be logged");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 7: Printf-style Logging
void testPrintfStyleLogging(TestHarness& harness) {
    harness.startTest("Printf-style Logging");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_printf.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        logger.log(LogLevel::INFO, "Integer: %d", 42);
        logger.log(LogLevel::INFO, "Float: %.2f", 3.14159);
        logger.log(LogLevel::INFO, "String: %s", "Hello");
        logger.log(LogLevel::INFO, "Multiple: %d %s %.1f", 10, "test", 2.5);
        
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged == 4, "All formatted messages should be logged");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 8: Flush Callback
void testFlushCallback(TestHarness& harness) {
    harness.startTest("Flush Callback");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_callback.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        std::atomic<int> callbackCount{0};
        std::atomic<size_t> totalEntriesInCallbacks{0};
        
        logger.setFlushCallback([&callbackCount, &totalEntriesInCallbacks](const std::vector<LogEntry>& entries) {
            callbackCount++;
            totalEntriesInCallbacks += entries.size();
        });
        
        logger.info("Message 1");
        logger.info("Message 2");
        logger.info("Message 3");
        
        logger.forceFlush();
        
        harness.assertCondition(callbackCount > 0, "Callback should have been called");
        harness.assertCondition(totalEntriesInCallbacks == 3, "Callback should receive all entries");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 9: Performance Stress Test
void testPerformance(TestHarness& harness) {
    harness.startTest("Performance Stress Test");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_performance.log";
        config.consoleOutput = false;
        config.bufferSize = 10000;
        config.asyncFlush = true;
        config.enableDeduplication = false;  // Disable for pure throughput test
        
        BufferedLogger logger(config);
        
        const int numMessages = 100000;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < numMessages; i++) {
            logger.info("Performance test message " + std::to_string(i));
        }
        
        logger.forceFlush();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        double throughput = (numMessages * 1000.0) / duration.count();
        
        std::cout << "\n  Throughput: " << std::fixed << std::setprecision(0) 
                  << throughput << " msgs/sec";
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged == numMessages, "All messages should be logged");
        harness.assertCondition(throughput > 10000, "Should achieve > 10k msgs/sec");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 10: Concurrent Readers/Writers
void testConcurrentAccess(TestHarness& harness) {
    harness.startTest("Concurrent Readers/Writers");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_concurrent.log";
        config.consoleOutput = false;
        config.asyncFlush = true;
        
        BufferedLogger logger(config);
        
        std::atomic<bool> stopFlag{false};
        std::vector<std::thread> threads;
        
        // Writer threads
        for (int i = 0; i < 5; i++) {
            threads.emplace_back([&logger, &stopFlag, i]() {
                int count = 0;
                while (!stopFlag) {
                    logger.info("Writer " + std::to_string(i) + " msg " + std::to_string(count++));
                    std::this_thread::sleep_for(1ms);
                }
            });
        }
        
        // Reader threads (checking stats)
        std::atomic<size_t> maxLogged{0};
        for (int i = 0; i < 3; i++) {
            threads.emplace_back([&logger, &stopFlag, &maxLogged]() {
                while (!stopFlag) {
                    auto stats = logger.getStats();
                    size_t current = stats.totalLogged;
                    size_t expected = maxLogged.load();
                    while (expected < current) {
                        if (maxLogged.compare_exchange_weak(expected, current)) {
                            break;
                        }
                    }
                    std::this_thread::sleep_for(5ms);
                }
            });
        }
        
        // Flush threads
        for (int i = 0; i < 2; i++) {
            threads.emplace_back([&logger, &stopFlag]() {
                while (!stopFlag) {
                    logger.flush();
                    std::this_thread::sleep_for(10ms);
                }
            });
        }
        
        // Run for 100ms
        std::this_thread::sleep_for(100ms);
        stopFlag = true;
        
        for (auto& t : threads) {
            t.join();
        }
        
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged > 0, "Should have logged messages");
        harness.assertCondition(stats.totalFlushed == stats.totalLogged, "All logged should be flushed");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 11: Global Logger Singleton
void testGlobalLogger(TestHarness& harness) {
    harness.startTest("Global Logger Singleton");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_global.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        
        GlobalLogger::configure(config);
        
        DRIVER_LOG_INFO("Global logger test");
        DRIVER_LOG_ERROR("Global error message");
        
        GlobalLogger::getInstance().forceFlush();
        
        auto& logger1 = GlobalLogger::getInstance();
        auto& logger2 = GlobalLogger::getInstance();
        
        harness.assertCondition(&logger1 == &logger2, "Should return same instance");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 12: Dynamic Configuration Changes
void testDynamicConfiguration(TestHarness& harness) {
    harness.startTest("Dynamic Configuration");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_dynamic.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        config.minimumLevel = LogLevel::INFO;
        
        BufferedLogger logger(config);
        
        logger.debug("Should not appear - level too low");
        logger.info("Should appear - level OK");
        
        // Change minimum level
        logger.setMinimumLevel(LogLevel::DEBUG);
        logger.debug("Should now appear");
        
        // Test deduplication toggle
        logger.enableDeduplication(true);
        for (int i = 0; i < 5; i++) {
            logger.info("Duplicate");
        }
        
        logger.enableDeduplication(false);
        for (int i = 0; i < 5; i++) {
            logger.info("Not deduplicated");
        }
        
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged > 7, "Should have logged messages");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 13: Edge Cases
void testEdgeCases(TestHarness& harness) {
    harness.startTest("Edge Cases");
    
    try {
        BufferedLogger::Config config;
        config.outputFile = "test_edge.log";
        config.consoleOutput = false;
        config.asyncFlush = false;
        
        BufferedLogger logger(config);
        
        // Empty message
        logger.info("");
        
        // Very long message
        std::string longMessage(10000, 'A');
        logger.info(longMessage);
        
        // Special characters
        logger.info("Special chars: \n\t\r\"'\\");
        
        // Unicode (if supported)
        logger.info("Unicode: 你好世界 🚀");
        
        // Null terminator in string
        std::string nullMessage = "Before";
        nullMessage.push_back('\0');
        nullMessage += "After";
        logger.info(nullMessage);
        
        logger.forceFlush();
        
        auto stats = logger.getStats();
        harness.assertCondition(stats.totalLogged == 5, "All edge cases should be logged");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Test 14: Shutdown and Cleanup
void testShutdownCleanup(TestHarness& harness) {
    harness.startTest("Shutdown and Cleanup");
    
    try {
        {
            BufferedLogger::Config config;
            config.outputFile = "test_shutdown.log";
            config.consoleOutput = false;
            config.asyncFlush = true;
            
            BufferedLogger logger(config);
            
            // Start logging
            std::atomic<bool> stopLogging{false};
            std::thread logThread([&logger, &stopLogging]() {
                while (!stopLogging) {
                    logger.info("Continuous message");
                    std::this_thread::sleep_for(1ms);
                }
            });
            
            std::this_thread::sleep_for(50ms);
            
            // Trigger shutdown
            logger.shutdown();
            
            stopLogging = true;
            logThread.join();
            
            // Try logging after shutdown (should be safe but no-op)
            logger.info("After shutdown");
            
            // Destructor will be called here
        }
        
        // Verify file exists and has content
        std::ifstream file("test_shutdown.log");
        harness.assertCondition(file.is_open(), "Log file should exist after shutdown");
        
        std::string line;
        int lineCount = 0;
        while (std::getline(file, line)) {
            lineCount++;
        }
        harness.assertCondition(lineCount > 0, "File should contain logged messages");
        
        harness.endTest(true);
    } catch (const std::exception& e) {
        harness.endTest(false, e.what());
    }
}

// Performance Benchmark
void runPerformanceBenchmark() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Performance Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    
    struct BenchmarkConfig {
        std::string name;
        bool asyncFlush;
        bool deduplication;
        size_t bufferSize;
        int numThreads;
    };
    
    std::vector<BenchmarkConfig> configs = {
        {"Single-thread, Sync, No Dedup", false, false, 1000, 1},
        {"Single-thread, Async, No Dedup", true, false, 1000, 1},
        {"Single-thread, Async, With Dedup", true, true, 1000, 1},
        {"Multi-thread (4), Async, No Dedup", true, false, 1000, 4},
        {"Multi-thread (8), Async, No Dedup", true, false, 1000, 8},
        {"Large Buffer (10k), Async", true, false, 10000, 1},
    };
    
    const int messagesPerThread = 10000;
    
    for (const auto& benchConfig : configs) {
        std::cout << "\nBenchmark: " << benchConfig.name << std::endl;
        
        BufferedLogger::Config config;
        config.outputFile = "benchmark.log";
        config.consoleOutput = false;
        config.asyncFlush = benchConfig.asyncFlush;
        config.enableDeduplication = benchConfig.deduplication;
        config.bufferSize = benchConfig.bufferSize;
        
        BufferedLogger logger(config);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < benchConfig.numThreads; t++) {
            threads.emplace_back([&logger, messagesPerThread, t]() {
                for (int i = 0; i < messagesPerThread; i++) {
                    logger.info("Thread " + std::to_string(t) + " msg " + std::to_string(i));
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        logger.forceFlush();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        int totalMessages = messagesPerThread * benchConfig.numThreads;
        double throughput = (totalMessages * 1000000.0) / duration.count();
        double latency = duration.count() / (double)totalMessages;
        
        std::cout << "  Total messages: " << totalMessages << std::endl;
        std::cout << "  Duration: " << duration.count() / 1000.0 << " ms" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << throughput << " msgs/sec" << std::endl;
        std::cout << "  Avg latency: " << std::fixed << std::setprecision(2) 
                  << latency << " μs/msg" << std::endl;
        
        auto stats = logger.getStats();
        std::cout << "  Total flushes: " << stats.totalFlushes << std::endl;
        if (benchConfig.deduplication) {
            std::cout << "  Deduplicated: " << stats.totalDeduplicated << std::endl;
        }
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Buffered Logger Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    TestHarness harness;
    
    // Run all tests
    testBasicLogging(harness);
    testDeduplication(harness);
    testThreadSafety(harness);
    testBufferOverflow(harness);
    testMemoryLimit(harness);
    testLogLevelFiltering(harness);
    testPrintfStyleLogging(harness);
    testFlushCallback(harness);
    testPerformance(harness);
    testConcurrentAccess(harness);
    testGlobalLogger(harness);
    testDynamicConfiguration(harness);
    testEdgeCases(harness);
    testShutdownCleanup(harness);
    
    harness.printSummary();
    
    // Run performance benchmark
    runPerformanceBenchmark();
    
    return 0;
}