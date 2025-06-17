#pragma once
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <queue>
#include <fstream>

class AdvancedSystemMonitor {
public:
    static AdvancedSystemMonitor* getInstance() {
        static AdvancedSystemMonitor instance;
        return &instance;
    }

    // 获取CPU使用率 (0.0 - 1.0)
    double getCPUUsage();

    // 获取内存压力 (0.0 - 1.0)
    double getMemoryPressure();

    // 获取线程池压力
    double getThreadPoolPressure(size_t activeThreads, size_t maxThreads);

    // 获取网络IO压力
    double getNetworkIOPressure();

    // 综合系统压力评估
    double getSystemLoadAverage();

    // 启动/停止监控
    void startMonitoring();
    void stopMonitoring();

    // 配置监控参数
    struct MonitorConfig {
        std::chrono::milliseconds updateInterval{ 1000 };
        double cpuWeight = 0.1;
        double memoryWeight = 0.5;
        double threadWeight = 0.5;
        double ioWeight = 0.1;
    };

    void setConfig(const MonitorConfig& config) { this->config = config; }

private:
    AdvancedSystemMonitor() = default;

    void monitoringLoop();
    void updateCPUStats();
    void updateMemoryStats();
    void updateIOStats();

    // 跨平台CPU使用率计算
    double calculateCPUUsage();

    // 跨平台内存使用情况
    double calculateMemoryUsage();

    std::atomic<double> cpuUsage{ 0.0 };
    std::atomic<double> memoryUsage{ 0.0 };
    std::atomic<double> ioUsage{ 0.0 };
    std::atomic<size_t> networkConnections{ 0 };

    std::thread monitoringThread;
    std::atomic<bool> isMonitoring{ false };
    std::atomic<bool> shouldStop{ false };

    MonitorConfig config;
    std::mutex configMutex;

    // 滑动窗口用于平滑数据
    std::queue<double> cpuHistory;
    std::queue<double> memoryHistory;
    std::mutex historyMutex;

    static constexpr size_t HISTORY_SIZE = 10;

    // 跨平台的系统信息获取
    struct SystemInfo {
        size_t totalMemory = 0;
        size_t availableMemory = 0;
        size_t processorCount = 0;
        std::chrono::steady_clock::time_point lastCpuTime;
        uint64_t lastCpuTotal = 0;
        uint64_t lastCpuIdle = 0;
    };

    SystemInfo sysInfo;
    std::mutex sysInfoMutex;
};

