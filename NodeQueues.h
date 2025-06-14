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
    MemoryPool(size_t initialBlockSize = 1024, size_t maxPoolSize = 10000);
    ~MemoryPool();

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

private:
    // 按大小分类的内存池
    std::unordered_map<size_t, moodycamel::ConcurrentQueue<void*>> sizePools;

    // 池子大小限制
    std::unordered_map<size_t, std::atomic<size_t>> poolSizes;

    const size_t maxPoolSize_;
    const size_t initialBlockSize_;

    // 统计信息
    mutable std::atomic<size_t> totalAllocated_{ 0 };
    mutable std::atomic<size_t> hitCount_{ 0 };
    mutable std::atomic<size_t> missCount_{ 0 };

    // 保护sizePools的读写锁
    mutable std::shared_mutex poolsMutex_;

    // 内存对齐辅助函数
    static size_t alignSize(size_t size);

    // 跨平台对齐内存分配
    static void* allocateAligned(size_t size, size_t alignment);
    static void freeAligned(void* ptr);

    // 获取或创建指定大小的池子
    moodycamel::ConcurrentQueue<void*>& getOrCreatePool(size_t size);
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

    // 获取内存池实例
    MemoryPool& getMemoryPool() { return memoryPool_; }

private:
    NodeQueues();

    // 禁用拷贝和赋值
    NodeQueues(const NodeQueues&) = delete;
    NodeQueues& operator=(const NodeQueues&) = delete;

    // MessageNode池子
    moodycamel::ConcurrentQueue<MessageNode*> messageNodePool_;

    // SendNode池子  
    moodycamel::ConcurrentQueue<SendNode*> sendNodePool_;

    // 池子大小限制
    static constexpr size_t MAX_MESSAGE_NODE_POOL_SIZE = 5000;
    static constexpr size_t MAX_SEND_NODE_POOL_SIZE = 5000;

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

    // 内存池实例
    MemoryPool memoryPool_;

    // 清理MessageNode状态
    void resetMessageNode(MessageNode* node);

    // 清理SendNode状态  
    void resetSendNode(SendNode* node);

    // 验证节点是否可以安全回收
    bool isNodeSafeToRecycle(MessageNode* node) const;
};