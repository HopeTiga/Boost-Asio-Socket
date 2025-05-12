#pragma once
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <boost/lockfree/queue.hpp>
#include "Singleton.h"
#include <new>
#include <cstdlib>

// 内存池层级定义
enum class BufferTier : size_t {
    SMALL = 4096,    // 4KB
    MEDIUM = 16384,  // 16KB
    LARGE = 65536,   // 64KB
    HUGE = 0         // 特殊值，表示动态分配，大小由请求决定
};

class BufferPool : public Singleton<BufferPool> {
public:
    BufferPool() {
        // 初始化时，为每个固定层级预分配一些缓冲区
        initTier(BufferTier::SMALL, 200);
        initTier(BufferTier::MEDIUM, 100);
        initTier(BufferTier::LARGE, 50);
    }

    ~BufferPool() {
        // 清理所有池中的缓冲区
        clearTier(BufferTier::SMALL);
        clearTier(BufferTier::MEDIUM);
        clearTier(BufferTier::LARGE);
    }

    // 获取缓冲区，返回 char* 指针
    char* getBuffer(size_t require_size) {
        BufferTier tier = selectTier(require_size);
        
        if (tier == BufferTier::HUGE) {
            // 对于非常大的请求，直接分配
            try {
                char* buffer = new (std::nothrow) char[require_size];
                if (!buffer) {
                    return nullptr; // 分配失败
                }
                // 在元数据映射中记录这个缓冲区的大小和层级
                std::lock_guard<std::mutex> lock(metadata_mutex_);
                buffer_metadata_[buffer] = {tier, require_size};
                return buffer;
            } catch (const std::bad_alloc&) {
                return nullptr; // 分配失败
            }
        }
        
        char* buffer = nullptr;
        // 尝试从对应层级的池中获取
        if (pools_[tier].free_buffers.pop(buffer)) {
            return buffer;
        }
        
        // 如果池为空，则新分配一个该层级的缓冲区
        try {
            size_t size = static_cast<size_t>(tier);
            buffer = new (std::nothrow) char[size];
            if (!buffer) {
                return nullptr; // 分配失败
            }
            // 在元数据映射中记录这个缓冲区的大小和层级
            std::lock_guard<std::mutex> lock(metadata_mutex_);
            buffer_metadata_[buffer] = {tier, size};
            return buffer;
        } catch (const std::bad_alloc&) {
            return nullptr; // 分配失败
        }
    }

    // 释放缓冲区
    void releaseBuffer(char* buffer) {
        if (!buffer) {
            return;
        }

        // 获取缓冲区的元数据
        BufferTier tier = BufferTier::HUGE; // 默认为HUGE，如果找不到元数据
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        auto it = buffer_metadata_.find(buffer);
        if (it != buffer_metadata_.end()) {
            tier = it->second.tier;
        } else {
            // 如果找不到元数据，直接删除并返回
            delete[] buffer;
            return;
        }

        if (tier == BufferTier::HUGE) {
            // HUGE 类型的缓冲区直接删除
            buffer_metadata_.erase(it);
            delete[] buffer;
            return;
        }
        
        // 对于固定层级的缓冲区，放回池中
        if (!pools_[tier].free_buffers.push(buffer)) {
            // 如果放回池失败，则直接删除
            buffer_metadata_.erase(it);
            delete[] buffer;
        }
    }

private:
    struct BufferMetadata {
        BufferTier tier;
        size_t size;
    };

    struct PoolTierQueue {
        // 每个层级一个无锁队列，初始容量128
        boost::lockfree::queue<char*> free_buffers{128}; 
    };

    // 使用 std::map 来存储不同层级的池
    std::map<BufferTier, PoolTierQueue> pools_;
    
    // 存储每个缓冲区的元数据（大小和层级）
    std::map<char*, BufferMetadata> buffer_metadata_;
    std::mutex metadata_mutex_; // 保护元数据映射的互斥锁

    void initTier(BufferTier tier, size_t count) {
        size_t size = static_cast<size_t>(tier);
        for(size_t i = 0; i < count; ++i) {
            try {
                char* buffer = new (std::nothrow) char[size];
                if (!buffer) {
                    break; // 分配失败，停止初始化
                }
                
                // 记录元数据
                {
                    std::lock_guard<std::mutex> lock(metadata_mutex_);
                    buffer_metadata_[buffer] = {tier, size};
                }
                
                if (!pools_[tier].free_buffers.push(buffer)) {
                    // 如果预初始化时队列就满了，直接删除
                    {
                        std::lock_guard<std::mutex> lock(metadata_mutex_);
                        buffer_metadata_.erase(buffer);
                    }
                    delete[] buffer;
                }
            } catch (const std::bad_alloc&) {
                // 预初始化分配失败，可能内存不足
                break; // 停止这个层级的初始化
            }
        }
    }

    void clearTier(BufferTier tier) {
        char* buffer = nullptr;
        while(pools_[tier].free_buffers.pop(buffer)) {
            {
                std::lock_guard<std::mutex> lock(metadata_mutex_);
                buffer_metadata_.erase(buffer);
            }
            delete[] buffer;
        }
    }

    BufferTier selectTier(size_t size) const {
        if (size == 0) return BufferTier::SMALL; // 处理0大小请求，返回最小的块
        if (size <= static_cast<size_t>(BufferTier::SMALL)) return BufferTier::SMALL;
        if (size <= static_cast<size_t>(BufferTier::MEDIUM)) return BufferTier::MEDIUM;
        if (size <= static_cast<size_t>(BufferTier::LARGE)) return BufferTier::LARGE;
        return BufferTier::HUGE; // 大于 LARGE 层级的都认为是 HUGE
    }
};