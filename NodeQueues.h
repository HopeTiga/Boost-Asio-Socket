#pragma once
#include "const.h"
#include "MessageNodes.h"
#include "Singleton.h"
#include "concurrentqueue.h"
#include <memory>
#include <atomic>
#include <chrono>
#include <cstdlib>

// 前向声明
class MemoryPool;

// 内存块结构
struct MemoryBlock {
    void* ptr;
    size_t size;
    bool inUse;
    std::chrono::steady_clock::time_point allocTime;

    MemoryBlock();
    MemoryBlock(void* p, size_t s);
};

// 内存池类 - 线程安全的内存管理
class MemoryPool {
public:
    ~MemoryPool();

    static MemoryPool* getInstance() {
        static MemoryPool instance;
        return &instance;
    }

    // 分配内存
    void* allocate(size_t size);

    // 释放内存回池子，失败则直接delete
    bool deallocate(void* ptr, size_t size);

    // 获取池子统计信息
    struct PoolStats {
        size_t totalAllocated;
        size_t poolSize;
        size_t hitRate;
        size_t missRate;
    };
    PoolStats getStats() const;

    // 清理过期内存块（定期调用）
    void cleanup();

    // 内存大小辅助函数（简化版本）
    static size_t alignSize(size_t size);

private:
    MemoryPool(size_t initialBlockSize = 1024, size_t maxPoolSize = 50);

    // 预定义内存池大小
    static constexpr size_t POOL_SIZES[] = {
        32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
    };
    static constexpr size_t POOL_COUNT = sizeof(POOL_SIZES) / sizeof(POOL_SIZES[0]);

    // 限制配置
    static constexpr size_t MAX_TOTAL_MEMORY = 100 * 1024 * 1024;  // 100MB
    static constexpr size_t MAX_BLOCK_SIZE = 64 * 1024;           // 64KB
    static constexpr size_t MAX_POOL_SIZE_PER_TYPE = 32;          // 32个块

    // 内存池数组
    moodycamel::ConcurrentQueue<void*> sizePools_[POOL_COUNT];
    std::atomic<size_t> poolSizes_[POOL_COUNT];

    // 内存跟踪
    mutable std::atomic<size_t> totalMemoryUsed_{ 0 };
    mutable std::atomic<size_t> totalPoolMemory_{ 0 };

    const size_t maxPoolSizePerType_;

    // 统计信息
    mutable std::atomic<size_t> totalAllocated_{ 0 };
    mutable std::atomic<size_t> hitCount_{ 0 };
    mutable std::atomic<size_t> missCount_{ 0 };

    // 内部辅助函数
    size_t getBestPoolIndex(size_t size) const;
    size_t getBestPoolSize(size_t size) const;
    bool shouldRejectAllocation(size_t size) const;
    bool shouldRejectDeallocation(size_t size, size_t poolIndex) const;

    // 内存分配函数
    static void* allocateAligned(size_t size);
    static void freeAligned(void* ptr);
};

// 节点池类 - 管理MessageNode和SendNode
class NodeQueues {
public:
    ~NodeQueues();

    static NodeQueues* getInstance() {
        static NodeQueues nodeQueues;
        return &nodeQueues;
    }

    // 获取节点（使用自定义删除器的版本）
    std::shared_ptr<MessageNode> acquireMessageNode(int64_t headLength = HEAD_TOTAL_LEN);
    std::shared_ptr<SendNode> acquireSendNode(const char* msg, int64_t max_length, short msgid);

    // 释放节点（空方法，自定义删除器自动处理）
    void releaseMessageNode(std::shared_ptr<MessageNode> node);
    void releaseSendNode(std::shared_ptr<SendNode> node);

    // 获取统计信息
    struct NodePoolStats {
        size_t messageNodePoolSize;
        size_t sendNodePoolSize;
        size_t messageNodeHitRate;
        size_t sendNodeHitRate;
        size_t totalMessageNodesCreated;
        size_t totalSendNodesCreated;
    };
    NodePoolStats getStats() const;

    // 清理池子
    void cleanup();

private:
    NodeQueues();

    // 禁用拷贝
    NodeQueues(const NodeQueues&) = delete;
    NodeQueues& operator=(const NodeQueues&) = delete;

    // 原始指针池子（不是shared_ptr池子）
    moodycamel::ConcurrentQueue<MessageNode*> messageNodePool_;
    moodycamel::ConcurrentQueue<SendNode*> sendNodePool_;

    // 池子大小限制
    static constexpr size_t MAX_MESSAGE_NODE_POOL_SIZE = 256;
    static constexpr size_t MAX_SEND_NODE_POOL_SIZE = 256;

    // 池子大小计数器
    std::atomic<size_t> messageNodePoolSize_{ 0 };
    std::atomic<size_t> sendNodePoolSize_{ 0 };

    // 统计信息
    mutable std::atomic<size_t> messageNodeHitCount_{ 0 };
    mutable std::atomic<size_t> messageNodeMissCount_{ 0 };
    mutable std::atomic<size_t> sendNodeHitCount_{ 0 };
    mutable std::atomic<size_t> sendNodeMissCount_{ 0 };
    mutable std::atomic<size_t> totalMessageNodesCreated_{ 0 };
    mutable std::atomic<size_t> totalSendNodesCreated_{ 0 };

    // 自定义删除器调用的方法
    void returnMessageNodeToPool(MessageNode* node);
    void returnSendNodeToPool(SendNode* node);

    // 重置节点状态
    void resetMessageNode(MessageNode* node);
    void resetSendNode(SendNode* node);
};