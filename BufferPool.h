#pragma once
#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <boost/lockfree/queue.hpp>
#include "Singleton.h"
#include <new>
#include <cstdlib>
#include <cassert>
#include <thread>
#include <chrono>

// 内存池层级定义
enum class BufferTier : uint8_t {
    SMALL = 0,   // 4KB
    MEDIUM = 1,  // 16KB  
    LARGE = 2,   // 64KB
    HUGE = 3     // 动态分配
};

// 层级大小常量
constexpr size_t TIER_SIZES[] = { 4096, 16384, 65536, 0 };
constexpr size_t TIER_COUNT = 3; // 除了HUGE外的固定层级数量

// 缓冲区头部信息，用于快速识别层级和大小
struct alignas(64) BufferHeader {
    uint32_t magic;      // 魔数，用于验证
    BufferTier tier;     // 层级
    uint8_t padding[3];  // 对齐填充
    size_t size;         // 实际大小

    static constexpr uint32_t MAGIC_VALUE = 0xDEADBEEF;

    BufferHeader(BufferTier t, size_t s) : magic(MAGIC_VALUE), tier(t), size(s) {}

    bool isValid() const { return magic == MAGIC_VALUE; }
};

class BufferPool : public Singleton<BufferPool> {
public:
    BufferPool() {
        // 初始化每个层级的池
        initTier(BufferTier::SMALL, 500);   // 增加小块数量
        initTier(BufferTier::MEDIUM, 200);  // 增加中块数量
        initTier(BufferTier::LARGE, 100);   // 增加大块数量

        // 启动统计线程
        stats_thread_ = std::thread(&BufferPool::statsWorker, this);
    }

    ~BufferPool() {
        // 停止统计线程
        stop_stats_ = true;
        if (stats_thread_.joinable()) {
            stats_thread_.join();
        }

        // 清理所有池
        for (size_t i = 0; i < TIER_COUNT; ++i) {
            clearTier(static_cast<BufferTier>(i));
        }
    }

    // 获取缓冲区，返回用户可用的指针
    char* getBuffer(size_t require_size) {
        if (require_size == 0) {
            require_size = 1; // 至少分配1字节
        }

        BufferTier tier = selectTier(require_size);

        // 更新统计
        ++total_requests_;

        if (tier == BufferTier::HUGE) {
            return allocateHugeBuffer(require_size);
        }

        // 尝试从池中获取
        char* raw_buffer = nullptr;
        if (pools_[static_cast<size_t>(tier)].free_buffers.pop(raw_buffer)) {
            ++pool_hits_;
            --tier_counters_[static_cast<size_t>(tier)]; // 减少计数
            return raw_buffer + sizeof(BufferHeader);
        }

        // 池为空，分配新的
        ++pool_misses_;
        return allocateNewBuffer(tier, TIER_SIZES[static_cast<size_t>(tier)]);
    }

    // 释放缓冲区
    void releaseBuffer(char* buffer) {
        if (!buffer) {
            return;
        }

        // 获取头部信息
        BufferHeader* header = reinterpret_cast<BufferHeader*>(buffer - sizeof(BufferHeader));

        // 验证魔数
        if (!header->isValid()) {
            // 可能是旧版本或损坏的缓冲区，直接删除
            std::free(buffer);
            return;
        }

        BufferTier tier = header->tier;

        if (tier == BufferTier::HUGE) {
            // HUGE 类型直接释放
            freeAlignedMemory(reinterpret_cast<char*>(header));
            return;
        }

        // 放回对应层级的池
        char* raw_buffer = reinterpret_cast<char*>(header);
        size_t tier_idx = static_cast<size_t>(tier);
        if (pools_[tier_idx].free_buffers.push(raw_buffer)) {
            ++tier_counters_[tier_idx]; // 增加计数
        }
        else {
            // 池满了，直接释放
            freeAlignedMemory(raw_buffer);
        }
    }

    // 获取统计信息
    struct Stats {
        size_t total_requests;
        size_t pool_hits;
        size_t pool_misses;
        size_t huge_allocations;
        std::array<size_t, TIER_COUNT> tier_counts;
    };

    Stats getStats() const {
        Stats stats;
        stats.total_requests = total_requests_.load();
        stats.pool_hits = pool_hits_.load();
        stats.pool_misses = pool_misses_.load();
        stats.huge_allocations = huge_allocations_.load();

        // boost::lockfree::queue没有size()方法，使用计数器
        for (size_t i = 0; i < TIER_COUNT; ++i) {
            stats.tier_counts[i] = tier_counters_[i].load();
        }

        return stats;
    }

private:
    struct PoolTierQueue {
        // 增加队列容量，减少扩容开销
        boost::lockfree::queue<char*> free_buffers{ 256 };
    };

    // 使用数组替代map，提高性能
    std::array<PoolTierQueue, TIER_COUNT> pools_;

    // 统计信息（原子操作，无锁）
    std::atomic<size_t> total_requests_{ 0 };
    std::atomic<size_t> pool_hits_{ 0 };
    std::atomic<size_t> pool_misses_{ 0 };
    std::atomic<size_t> huge_allocations_{ 0 };

    // 每个层级的缓冲区计数器
    std::array<std::atomic<size_t>, TIER_COUNT> tier_counters_;

    // 统计线程
    std::thread stats_thread_;
    std::atomic<bool> stop_stats_{ false };

    // 分配HUGE级别的缓冲区
    char* allocateHugeBuffer(size_t size) {
        ++huge_allocations_;

        size_t total_size = sizeof(BufferHeader) + size;
        char* raw_buffer = nullptr;

        // 使用对齐内存分配
#ifdef _WIN32
        raw_buffer = static_cast<char*>(_aligned_malloc(total_size, 64));
#else
        if (posix_memalign(reinterpret_cast<void**>(&raw_buffer), 64, total_size) != 0) {
            raw_buffer = nullptr;
        }
#endif

        if (!raw_buffer) {
            return nullptr;
        }

        // 初始化头部
        new (raw_buffer) BufferHeader(BufferTier::HUGE, size);

        return raw_buffer + sizeof(BufferHeader);
    }

    // 分配新的固定层级缓冲区
    char* allocateNewBuffer(BufferTier tier, size_t size) {
        size_t total_size = sizeof(BufferHeader) + size;

        // 使用std::aligned_alloc或posix_memalign进行对齐分配
        char* raw_buffer = nullptr;

#ifdef _WIN32
        raw_buffer = static_cast<char*>(_aligned_malloc(total_size, 64));
#else
        if (posix_memalign(reinterpret_cast<void**>(&raw_buffer), 64, total_size) != 0) {
            raw_buffer = nullptr;
        }
#endif

        if (!raw_buffer) {
            return nullptr;
        }

        // 初始化头部
        new (raw_buffer) BufferHeader(tier, size);

        return raw_buffer + sizeof(BufferHeader);
    }

    // 释放对齐内存
    void freeAlignedMemory(char* ptr) {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    // 初始化指定层级的池
    void initTier(BufferTier tier, size_t count) {
        size_t tier_idx = static_cast<size_t>(tier);
        size_t size = TIER_SIZES[tier_idx];

        for (size_t i = 0; i < count; ++i) {
            char* buffer = allocateNewBuffer(tier, size);
            if (!buffer) {
                break; // 分配失败，停止初始化
            }

            // 转回原始指针存储
            char* raw_buffer = buffer - sizeof(BufferHeader);
            if (pools_[tier_idx].free_buffers.push(raw_buffer)) {
                ++tier_counters_[tier_idx]; // 增加计数
            }
            else {
                // 队列满了，释放这个缓冲区
                freeAlignedMemory(raw_buffer);
                break;
            }
        }
    }

    // 清理指定层级的池
    void clearTier(BufferTier tier) {
        size_t tier_idx = static_cast<size_t>(tier);
        char* raw_buffer = nullptr;

        while (pools_[tier_idx].free_buffers.pop(raw_buffer)) {
            freeAlignedMemory(raw_buffer);
            --tier_counters_[tier_idx]; // 减少计数
        }
    }

    // 选择合适的层级
    BufferTier selectTier(size_t size) const {
        if (size <= TIER_SIZES[0]) return BufferTier::SMALL;
        if (size <= TIER_SIZES[1]) return BufferTier::MEDIUM;
        if (size <= TIER_SIZES[2]) return BufferTier::LARGE;
        return BufferTier::HUGE;
    }

    // 统计信息工作线程
    void statsWorker() {
        while (!stop_stats_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));

            // 可以在这里添加定期统计输出或自动调整逻辑
            // 例如：根据命中率动态调整池大小
        }
    }
};

// 便利的RAII缓冲区包装器
class BufferGuard {
private:
    char* buffer_;

public:
    explicit BufferGuard(size_t size) {
        buffer_ = BufferPool::getInstance()->getBuffer(size);
    }

    ~BufferGuard() {
        if (buffer_) {
            BufferPool::getInstance()->releaseBuffer(buffer_);
        }
    }

    // 禁用拷贝
    BufferGuard(const BufferGuard&) = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;

    // 支持移动
    BufferGuard(BufferGuard&& other) noexcept
        : buffer_(other.buffer_) {
        other.buffer_ = nullptr;
    }

    BufferGuard& operator=(BufferGuard&& other) noexcept {
        if (this != &other) {
            if (buffer_) {
                BufferPool::getInstance()->releaseBuffer(buffer_);
            }
            buffer_ = other.buffer_;
            other.buffer_ = nullptr;
        }
        return *this;
    }

    char* get() const { return buffer_; }
    operator bool() const { return buffer_ != nullptr; }

    char* release() {
        char* tmp = buffer_;
        buffer_ = nullptr;
        return tmp;
    }
};