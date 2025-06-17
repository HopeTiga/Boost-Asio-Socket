#define NOMINMAX
#include "AdvancedSystemMonitor.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include "Utils.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

double AdvancedSystemMonitor::getCPUUsage() {
    return cpuUsage.load();
}

double AdvancedSystemMonitor::getMemoryPressure() {
    return memoryUsage.load();
}

double AdvancedSystemMonitor::getThreadPoolPressure(size_t activeThreads, size_t maxThreads) {
    if (maxThreads == 0) return 0.0;
    return std::min(1.0, static_cast<double>(activeThreads) / maxThreads);
}

double AdvancedSystemMonitor::getNetworkIOPressure() {
    return ioUsage.load();
}

double AdvancedSystemMonitor::getSystemLoadAverage() {
    std::lock_guard<std::mutex> lock(configMutex);

    double cpu = getCPUUsage();
    double memory = getMemoryPressure();
    double io = getNetworkIOPressure();

    // 简单的线程压力估算（基于硬件并发数）
    size_t hwConcurrency = std::thread::hardware_concurrency();
    double thread = getThreadPoolPressure(hwConcurrency, hwConcurrency * 2);

    return (cpu * config.cpuWeight +
        memory * config.memoryWeight +
        thread * config.threadWeight +
        io * config.ioWeight);
}

void AdvancedSystemMonitor::startMonitoring() {
    if (!isMonitoring.exchange(true)) {
        shouldStop = false;

        // 初始化系统信息
        {
            std::lock_guard<std::mutex> lock(sysInfoMutex);
            sysInfo.processorCount = std::thread::hardware_concurrency();
            sysInfo.lastCpuTime = std::chrono::steady_clock::now();

#ifdef _WIN32
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&memInfo);
            sysInfo.totalMemory = memInfo.ullTotalPhys;
#elif defined(__linux__)
            struct sysinfo si;
            sysinfo(&si);
            sysInfo.totalMemory = si.totalram * si.mem_unit;
#elif defined(__APPLE__)
            int mib[2];
            size_t length = sizeof(sysInfo.totalMemory);
            mib[0] = CTL_HW;
            mib[1] = HW_MEMSIZE;
            sysctl(mib, 2, &sysInfo.totalMemory, &length, NULL, 0);
#endif
        }
        LOG_INFO("AdvancedSystemMonitor started monitoring.");
        monitoringThread = std::thread([this]() { monitoringLoop(); });

    }
}

void AdvancedSystemMonitor::stopMonitoring() {
    if (isMonitoring.exchange(false)) {
        shouldStop = true;
        if (monitoringThread.joinable()) {
            monitoringThread.join();
        }
		LOG_INFO("AdvancedSystemMonitor stopped monitoring.");
    }
}

void AdvancedSystemMonitor::monitoringLoop() {
    while (!shouldStop && isMonitoring) {
        updateCPUStats();
        updateMemoryStats();
        updateIOStats();

        std::this_thread::sleep_for(config.updateInterval);
    }
}

void AdvancedSystemMonitor::updateCPUStats() {
    double usage = calculateCPUUsage();
    cpuUsage.store(usage);

    // 更新历史记录用于平滑
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        cpuHistory.push(usage);
        if (cpuHistory.size() > HISTORY_SIZE) {
            cpuHistory.pop();
        }
    }
}

void AdvancedSystemMonitor::updateMemoryStats() {
    double usage = calculateMemoryUsage();
    memoryUsage.store(usage);

    // 更新历史记录
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        memoryHistory.push(usage);
        if (memoryHistory.size() > HISTORY_SIZE) {
            memoryHistory.pop();
        }
    }
}

void AdvancedSystemMonitor::updateIOStats() {
    // 简单的IO压力估算 - 基于网络连接数
    // 在实际应用中，可以监控更多IO指标
    size_t connections = networkConnections.load();
    double pressure = std::min(1.0, connections / 20000.0); // 假设1000个连接为满负载
    ioUsage.store(pressure);
}

double AdvancedSystemMonitor::calculateCPUUsage() {
#ifdef _WIN32
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors = 0;
    static bool first = true;

    HANDLE hProcess = GetCurrentProcess();
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    double percent = 0.0;

    if (first) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));

        GetProcessTimes(hProcess, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
        first = false;
        return 0.0;
    }

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));

    GetProcessTimes(hProcess, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));

    percent = (sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart);
    percent /= (now.QuadPart - lastCPU.QuadPart);
    percent /= numProcessors;
    lastCPU = now;
    lastUserCPU = user;
    lastSysCPU = sys;

    return percent;

#elif defined(__linux__)
    static unsigned long long lastTotalUser = 0, lastTotalUserLow = 0, lastTotalSys = 0, lastTotalIdle = 0;
    static bool first = true;

    std::ifstream file("/proc/stat");
    std::string line;
    if (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string cpu;
        unsigned long long user, nice, system, idle;
        iss >> cpu >> user >> nice >> system >> idle;

        if (first) {
            lastTotalUser = user;
            lastTotalUserLow = nice;
            lastTotalSys = system;
            lastTotalIdle = idle;
            first = false;
            return 0.0;
        }

        unsigned long long totalUser = user - lastTotalUser;
        unsigned long long totalUserLow = nice - lastTotalUserLow;
        unsigned long long totalSys = system - lastTotalSys;
        unsigned long long totalIdle = idle - lastTotalIdle;

        unsigned long long total = totalUser + totalUserLow + totalSys + totalIdle;

        double percent = (total - totalIdle) / static_cast<double>(total);

        lastTotalUser = user;
        lastTotalUserLow = nice;
        lastTotalSys = system;
        lastTotalIdle = idle;

        return percent;
    }
    return 0.0;

#elif defined(__APPLE__)
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        unsigned long long totalTicks = 0;
        for (int i = 0; i < CPU_STATE_MAX; i++) {
            totalTicks += cpuinfo.cpu_ticks[i];
        }

        std::lock_guard<std::mutex> lock(sysInfoMutex);
        if (sysInfo.lastCpuTotal > 0) {
            unsigned long long totalTicksSinceLastTime = totalTicks - sysInfo.lastCpuTotal;
            unsigned long long idleTicksSinceLastTime = cpuinfo.cpu_ticks[CPU_STATE_IDLE] - sysInfo.lastCpuIdle;

            if (totalTicksSinceLastTime > 0) {
                double ret = 1.0 - ((double)idleTicksSinceLastTime) / totalTicksSinceLastTime;
                sysInfo.lastCpuTotal = totalTicks;
                sysInfo.lastCpuIdle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
                return ret;
            }
        }

        sysInfo.lastCpuTotal = totalTicks;
        sysInfo.lastCpuIdle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
    }
    return 0.0;

#else
    // 通用估算方法 - 基于线程活动
    static auto lastTime = std::chrono::steady_clock::now();
    static std::atomic<int> busyCount{ 0 };

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);

    if (elapsed.count() >= 1000) {
        int currentBusy = busyCount.exchange(0);
        double usage = std::min(1.0, currentBusy / 1000.0);
        lastTime = now;
        return usage;
    }

    busyCount++;
    return cpuUsage.load();
#endif
}

double AdvancedSystemMonitor::calculateMemoryUsage() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    DWORDLONG totalVirtualMem = memInfo.ullTotalPageFile;
    DWORDLONG virtualMemUsed = memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;

    return static_cast<double>(virtualMemUsed) / totalVirtualMem;

#elif defined(__linux__)
    struct sysinfo memInfo;
    sysinfo(&memInfo);

    long long totalMem = memInfo.totalram * memInfo.mem_unit;
    long long freeMem = memInfo.freeram * memInfo.mem_unit;
    long long usedMem = totalMem - freeMem;

    return static_cast<double>(usedMem) / totalMem;

#elif defined(__APPLE__)
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = sizeof(vm_stat) / sizeof(natural_t);

    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {

        long long total_mem = (vm_stat.free_count + vm_stat.active_count + vm_stat.inactive_count + vm_stat.wire_count) * page_size;
        long long used_mem = (vm_stat.active_count + vm_stat.inactive_count + vm_stat.wire_count) * page_size;

        return static_cast<double>(used_mem) / total_mem;
    }
    return 0.0;

#else
    // 通用估算 - 返回固定值或基于进程内存使用
    return 0.5; // 50% 作为默认估算
#endif
}