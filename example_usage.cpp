#include "buffered_logger.h"
#include <iostream>
#include <thread>
#include <random>
#include <chrono>

using namespace DisplayDriver;
using namespace std::chrono_literals;

// Simulate display driver components
class DisplayDriverSimulator {
private:
    BufferedLogger& m_logger;
    std::atomic<bool> m_running{true};
    std::vector<std::thread> m_threads;
    
public:
    explicit DisplayDriverSimulator(BufferedLogger& logger) : m_logger(logger) {}
    
    void start() {
        std::cout << "Starting Display Driver Simulator..." << std::endl;
        
        // Simulate VSYNC handler
        m_threads.emplace_back(&DisplayDriverSimulator::vsyncHandler, this);
        
        // Simulate command buffer processor
        m_threads.emplace_back(&DisplayDriverSimulator::commandBufferProcessor, this);
        
        // Simulate memory manager
        m_threads.emplace_back(&DisplayDriverSimulator::memoryManager, this);
        
        // Simulate error handler
        m_threads.emplace_back(&DisplayDriverSimulator::errorHandler, this);
        
        // Simulate performance monitor
        m_threads.emplace_back(&DisplayDriverSimulator::performanceMonitor, this);
    }
    
    void stop() {
        std::cout << "Stopping Display Driver Simulator..." << std::endl;
        m_running = false;
        for (auto& t : m_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    
private:
    void vsyncHandler() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> frametime_dist(14, 18);  // 14-18ms frame times
        
        int frameCount = 0;
        while (m_running) {
            auto frameStart = std::chrono::steady_clock::now();
            
            // Log VSYNC event (high frequency, good candidate for deduplication)
            m_logger.trace("VSYNC interrupt received");
            
            // Occasionally log frame timing
            if (frameCount % 60 == 0) {  // Every second at 60fps
                m_logger.info("Frame " + std::to_string(frameCount) + " completed");
            }
            
            // Simulate occasional tearing
            if (frameCount % 500 == 499) {
                m_logger.warning("Screen tearing detected at frame " + std::to_string(frameCount));
            }
            
            frameCount++;
            
            // Simulate frame time
            std::this_thread::sleep_for(std::chrono::milliseconds(frametime_dist(gen)));
        }
    }
    
    void commandBufferProcessor() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> cmd_dist(1, 10);
        std::uniform_int_distribution<> size_dist(1024, 65536);
        
        const char* commands[] = {
            "DRAW_INDEXED", "CLEAR", "PRESENT", "SET_VIEWPORT", 
            "BIND_PIPELINE", "UPDATE_BUFFER", "COPY_TEXTURE"
        };
        
        while (m_running) {
            int numCommands = cmd_dist(gen);
            
            for (int i = 0; i < numCommands; i++) {
                const char* cmd = commands[i % 7];
                int size = size_dist(gen);
                
                // Log command processing (will be deduplicated if repeated)
                m_logger.debug("Processing command: " + std::string(cmd) + 
                             " [size: " + std::to_string(size) + " bytes]");
                
                // Simulate processing time
                std::this_thread::sleep_for(std::chrono::microseconds(size / 100));
            }
            
            std::this_thread::sleep_for(2ms);
        }
    }
    
    void memoryManager() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> alloc_dist(1024, 1024 * 1024);  // 1KB to 1MB
        std::uniform_real_distribution<> pressure_dist(0.0, 1.0);
        
        size_t totalAllocated = 0;
        const size_t maxMemory = 2ULL * 1024 * 1024 * 1024;  // 2GB VRAM simulation
        
        while (m_running) {
            size_t allocSize = alloc_dist(gen);
            
            if (totalAllocated + allocSize < maxMemory) {
                totalAllocated += allocSize;
                m_logger.trace("Allocated " + std::to_string(allocSize) + 
                             " bytes of VRAM [Total: " + std::to_string(totalAllocated) + "]");
            } else {
                m_logger.warning("VRAM allocation failed - insufficient memory");
                
                // Simulate cleanup
                totalAllocated = totalAllocated * 0.7;  // Free 30%
                m_logger.info("Performed VRAM garbage collection, freed memory");
            }
            
            // Check memory pressure
            double pressure = (double)totalAllocated / maxMemory;
            if (pressure > 0.9) {
                m_logger.critical("Critical VRAM pressure: " + 
                                std::to_string(int(pressure * 100)) + "% utilized");
            } else if (pressure > 0.75) {
                m_logger.warning("High VRAM usage: " + 
                               std::to_string(int(pressure * 100)) + "% utilized");
            }
            
            std::this_thread::sleep_for(50ms);
        }
    }
    
    void errorHandler() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> error_dist(0, 1000);
        
        const char* errors[] = {
            "GPU timeout detected",
            "Invalid command buffer",
            "Shader compilation failed",
            "Surface lost",
            "Device removed",
            "TDR (Timeout Detection and Recovery) triggered"
        };
        
        while (m_running) {
            int errorChance = error_dist(gen);
            
            if (errorChance < 5) {  // 0.5% chance of critical error
                m_logger.critical(errors[5]);
                m_logger.error("Initiating GPU reset sequence");
                std::this_thread::sleep_for(100ms);  // Simulate reset
                m_logger.info("GPU reset completed successfully");
            } else if (errorChance < 20) {  // 2% chance of regular error
                m_logger.error(errors[errorChance % 5]);
            } else if (errorChance < 100) {  // 10% chance of warning
                m_logger.warning("GPU temperature threshold approaching");
            }
            
            std::this_thread::sleep_for(100ms);
        }
    }
    
    void performanceMonitor() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> fps_dist(55, 65);
        std::uniform_int_distribution<> util_dist(40, 100);
        
        while (m_running) {
            int fps = fps_dist(gen);
            int gpuUtil = util_dist(gen);
            int vramUtil = util_dist(gen);
            
            // Log performance metrics
            m_logger.log(LogLevel::INFO, 
                        "Performance: FPS=%d, GPU=%d%%, VRAM=%d%%",
                        fps, gpuUtil, vramUtil);
            
            // Detect performance issues
            if (fps < 60) {
                m_logger.warning("Frame rate below target: " + std::to_string(fps) + " FPS");
            }
            
            if (gpuUtil > 95) {
                m_logger.warning("GPU bottleneck detected: " + std::to_string(gpuUtil) + "% utilization");
            }
            
            std::this_thread::sleep_for(1s);  // Report every second
        }
    }
};

int main() {
    std::cout << "===========================================\n";
    std::cout << "Display Driver Buffered Logger Example\n";
    std::cout << "===========================================\n\n";
    
    // Configure logger for display driver scenario
    BufferedLogger::Config config;
    config.outputFile = "display_driver.log";
    config.consoleOutput = true;  // Also show on console for demo
    config.bufferSize = 5000;  // Large buffer for high-throughput
    config.maxMemoryBytes = 10 * 1024 * 1024;  // 10MB max
    config.flushInterval = 100ms;  // Flush every 100ms
    config.enableDeduplication = true;  // Enable deduplication
    config.deduplicationWindowSize = 1000;
    config.deduplicationTimeWindow = 1000ms;  // 1 second dedup window
    config.minimumLevel = LogLevel::DEBUG;  // Log everything except TRACE in production
    config.asyncFlush = true;  // Async for performance
    
    // Create logger
    BufferedLogger logger(config);
    
    // Set up custom flush callback for telemetry
    size_t telemetryCount = 0;
    logger.setFlushCallback([&telemetryCount](const std::vector<LogEntry>& entries) {
        // In real scenario, this could send to telemetry service
        telemetryCount += entries.size();
        
        // Count critical errors
        int criticalCount = 0;
        for (const auto& entry : entries) {
            if (entry.level == LogLevel::CRITICAL) {
                criticalCount++;
            }
        }
        
        if (criticalCount > 0) {
            std::cout << "\n[TELEMETRY ALERT] " << criticalCount 
                      << " critical errors detected!\n" << std::endl;
        }
    });
    
    logger.info("Display Driver Logger initialized");
    logger.info("Version: 1.0.0");
    logger.info("Configuration: High-performance mode enabled");
    
    // Create and start driver simulator
    DisplayDriverSimulator simulator(logger);
    simulator.start();
    
    // Run for 10 seconds
    std::cout << "\nSimulation running for 10 seconds...\n" << std::endl;
    std::this_thread::sleep_for(10s);
    
    // Stop simulation
    simulator.stop();
    
    // Final flush
    logger.info("Display Driver shutting down");
    logger.flush();
    
    // Wait a bit for async flush
    std::this_thread::sleep_for(200ms);
    
    // Print statistics
    auto stats = logger.getStats();
    std::cout << "\n===========================================\n";
    std::cout << "Logger Statistics:\n";
    std::cout << "-------------------------------------------\n";
    std::cout << "Total logs processed: " << stats.totalLogs << "\n";
    std::cout << "Total messages: " << stats.totalMessages << "\n";
    std::cout << "Total critical errors: " << stats.criticalErrors << "\n";
    std::cout << "Total warnings: " << stats.warnings << "\n";
    std::cout << "Total info messages: " << stats.infoMessages << "\n";
    std::cout << "Total debug messages: " << stats.debugMessages << "\n";
    std::cout << "Total trace messages: " << stats.traceMessages << "\n";
    std::cout << "Total errors: " << stats.errors << "\n";