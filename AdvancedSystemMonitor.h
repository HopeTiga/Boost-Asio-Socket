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

    // ��ȡCPUʹ���� (0.0 - 1.0)
    double getCPUUsage();

    // ��ȡ�ڴ�ѹ�� (0.0 - 1.0)
    double getMemoryPressure();

    // ��ȡ�̳߳�ѹ��
    double getThreadPoolPressure(size_t activeThreads, size_t maxThreads);

    // ��ȡ����IOѹ��
    double getNetworkIOPressure();

    // �ۺ�ϵͳѹ������
    double getSystemLoadAverage();

    // ����/ֹͣ���
    void startMonitoring();
    void stopMonitoring();

    // ���ü�ز���
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

    // ��ƽ̨CPUʹ���ʼ���
    double calculateCPUUsage();

    // ��ƽ̨�ڴ�ʹ�����
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

    // ������������ƽ������
    std::queue<double> cpuHistory;
    std::queue<double> memoryHistory;
    std::mutex historyMutex;

    static constexpr size_t HISTORY_SIZE = 10;

    // ��ƽ̨��ϵͳ��Ϣ��ȡ
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

