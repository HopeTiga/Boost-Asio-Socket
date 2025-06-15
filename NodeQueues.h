#pragma once
#include "const.h"
#include "MessageNodes.h"
#include "Singleton.h"
#include "concurrentqueue.h"
#include <memory>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <shared_mutex>
#include <chrono>

#ifdef _WIN32
#include <malloc.h>  // for _aligned_malloc/_aligned_free
#else
#include <stdlib.h>  // for posix_memalign/free
#endif

// 内存块结构
struct MemoryBlock {
    void* ptr;
    size_t size;
    bool inUse;
    std::chrono::steady_clock::time_point allocTime;

    MemoryBlock() : ptr(nullptr), size(0), inUse(false) {}
    MemoryBlock(void* p, size_t s) : ptr(p), size(s), inUse(true),
        allocTime(std::chrono::steady_clock::now()) {
    }
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

    // 内存对齐辅助函数
    static size_t alignSize(size_t size);

private:
    MemoryPool(size_t initialBlockSize = 1024, size_t maxPoolSize = 50);

    // 🔧 优化：预定义内存池大小，减少池子类型数量
    static constexpr size_t POOL_SIZES[] = {
        32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
    };
    static constexpr size_t POOL_COUNT = sizeof(POOL_SIZES) / sizeof(POOL_SIZES[0]);

    // 🔧 优化：降低限制，更加保守
    static constexpr size_t MAX_TOTAL_MEMORY = 100 * 1024 * 1024;  // 100MB总限制
    static constexpr size_t MAX_BLOCK_SIZE = 64 * 1024;           // 单个块最大64KB
    static constexpr size_t MAX_POOL_SIZE_PER_TYPE = 32;          // 每种大小最多32个块

    // 🔧 优化：使用数组而不是map，提高性能
    moodycamel::ConcurrentQueue<void*> sizePools_[POOL_COUNT];
    std::atomic<size_t> poolSizes_[POOL_COUNT];

    // 🔧 优化：更精确的内存跟踪
    mutable std::atomic<size_t> totalMemoryUsed_{ 0 };     // 实际使用的内存
    mutable std::atomic<size_t> totalPoolMemory_{ 0 };     // 池子中的内存

    const size_t maxPoolSizePerType_;

    // 统计信息
    mutable std::atomic<size_t> totalAllocated_{ 0 };
    mutable std::atomic<size_t> hitCount_{ 0 };
    mutable std::atomic<size_t> missCount_{ 0 };

    // 🔧 优化：更轻量的锁（如果需要的话）
    mutable std::atomic_flag initFlag_ = ATOMIC_FLAG_INIT;

    // 🔧 优化：内部辅助函数
    size_t getBestPoolIndex(size_t size) const;
    size_t getBestPoolSize(size_t size) const;
    bool shouldRejectAllocation(size_t size) const;
    bool shouldRejectDeallocation(size_t size, size_t poolIndex) const;

    // 跨平台对齐内存分配 - 优化对齐策略
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

    // 获取MessageNode，优先从池子取，否则新建
    MessageNode* acquireMessageNode(int64_t headLength = HEAD_TOTAL_LEN);

    // 获取SendNode，优先从池子取，否则新建  
    SendNode* acquireSendNode(const char* msg, int64_t max_length, short msgid);

    // 释放MessageNode，优先回池子，失败则删除
    void releaseMessageNode(MessageNode* node);

    // 释放SendNode，优先回池子，失败则删除
    void releaseSendNode(SendNode* node);

    // 获取池子统计信息
    struct NodePoolStats {
        size_t messageNodePoolSize;
        size_t sendNodePoolSize;
        size_t messageNodeHitRate;
        size_t sendNodeHitRate;
        size_t totalMessageNodesCreated;
        size_t totalSendNodesCreated;
    };
    NodePoolStats getStats() const;

    // 清理池子中的过期节点
    void cleanup();

private:
    NodeQueues();

    // 禁用拷贝和赋值
    NodeQueues(const NodeQueues&) = delete;
    NodeQueues& operator=(const NodeQueues&) = delete;

    // MessageNode池子
    moodycamel::ConcurrentQueue<MessageNode*> messageNodePool_;

    // SendNode池子  
    moodycamel::ConcurrentQueue<SendNode*> sendNodePool_;

    // 🔧 优化：降低池子大小限制
    static constexpr size_t MAX_MESSAGE_NODE_POOL_SIZE = 256;
    static constexpr size_t MAX_SEND_NODE_POOL_SIZE = 256;

    // 当前池子大小计数器
    std::atomic<size_t> messageNodePoolSize_{ 0 };
    std::atomic<size_t> sendNodePoolSize_{ 0 };

    // 统计信息
    mutable std::atomic<size_t> messageNodeHitCount_{ 0 };
    mutable std::atomic<size_t> messageNodeMissCount_{ 0 };
    mutable std::atomic<size_t> sendNodeHitCount_{ 0 };
    mutable std::atomic<size_t> sendNodeMissCount_{ 0 };
    mutable std::atomic<size_t> totalMessageNodesCreated_{ 0 };
    mutable std::atomic<size_t> totalSendNodesCreated_{ 0 };

    // 清理MessageNode状态
    void resetMessageNode(MessageNode* node);

    // 清理SendNode状态  
    void resetSendNode(SendNode* node);

    // 验证节点是否可以安全回收
    bool isNodeSafeToRecycle(MessageNode* node) const;
};