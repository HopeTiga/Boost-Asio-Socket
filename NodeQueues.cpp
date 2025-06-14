#include "NodeQueues.h"
#include "FastMemcpy_Avx.h"
#include <algorithm>
#include <chrono>

// ============================================================================
// 跨平台内存对齐分配函数
// ============================================================================

void* MemoryPool::allocateAligned(size_t size, size_t alignment) {
#ifdef _WIN32
    // Windows平台使用_aligned_malloc
    return _aligned_malloc(size, alignment);
#else
    // Unix/Linux平台使用posix_memalign
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) == 0) {
        return ptr;
    }
    return nullptr;
#endif
}

void MemoryPool::freeAligned(void* ptr) {
    if (!ptr) return;

#ifdef _WIN32
    // Windows平台使用_aligned_free
    _aligned_free(ptr);
#else
    // Unix/Linux平台使用普通free
    free(ptr);
#endif
}

// ============================================================================
// MemoryPool 实现
// ============================================================================

MemoryPool::MemoryPool(size_t initialBlockSize, size_t maxPoolSize)
    : maxPoolSize_(maxPoolSize), initialBlockSize_(initialBlockSize) {
}

MemoryPool::~MemoryPool() {
    // 清理所有池子中的内存
    std::unique_lock<std::shared_mutex> lock(poolsMutex_);

    for (auto& [size, pool] : sizePools) {
        void* ptr = nullptr;
        while (pool.try_dequeue(ptr)) {
            if (ptr) {
                freeAligned(ptr);
            }
        }
    }
    sizePools.clear();
}

size_t MemoryPool::alignSize(size_t size) {
    // 对齐到8字节边界
    return (size + 7) & ~7;
}

moodycamel::ConcurrentQueue<void*>& MemoryPool::getOrCreatePool(size_t size) {
    {
        std::shared_lock<std::shared_mutex> readLock(poolsMutex_);
        auto it = sizePools.find(size);
        if (it != sizePools.end()) {
            return it->second;
        }
    }

    // 需要创建新池子
    std::unique_lock<std::shared_mutex> writeLock(poolsMutex_);

    // 双重检查
    auto it = sizePools.find(size);
    if (it != sizePools.end()) {
        return it->second;
    }

    // 创建新池子
    auto [newIt, inserted] = sizePools.emplace(size, moodycamel::ConcurrentQueue<void*>());
    poolSizes[size].store(0);

    return newIt->second;
}

void* MemoryPool::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    size_t alignedSize = alignSize(size);

    // 尝试从池子获取
    auto& pool = getOrCreatePool(alignedSize);
    void* ptr = nullptr;

    if (pool.try_dequeue(ptr) && ptr != nullptr) {
        poolSizes[alignedSize].fetch_sub(1);
        hitCount_.fetch_add(1);
        return ptr;
    }

    // 池子中没有，分配新内存 - 跨平台对齐分配
    ptr = allocateAligned(alignedSize, 32); // 32字节对齐，适配AVX
    if (ptr) {
        totalAllocated_.fetch_add(1);
        missCount_.fetch_add(1);
    }

    return ptr;
}

bool MemoryPool::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return false;
    }

    size_t alignedSize = alignSize(size);

    // 检查池子大小限制
    size_t currentPoolSize = poolSizes[alignedSize].load();
    if (currentPoolSize >= maxPoolSize_) {
        freeAligned(ptr);
        return false;
    }

    // 尝试放回池子
    auto& pool = getOrCreatePool(alignedSize);

    if (pool.enqueue(ptr)) {
        poolSizes[alignedSize].fetch_add(1);
        return true;
    }

    // 放回失败，直接释放
    freeAligned(ptr);
    return false;
}

MemoryPool::PoolStats MemoryPool::getStats() const {
    PoolStats stats;
    stats.totalAllocated = totalAllocated_.load();

    // 计算总池子大小
    stats.poolSize = 0;
    {
        std::shared_lock<std::shared_mutex> lock(poolsMutex_);
        for (const auto& [size, count] : poolSizes) {
            stats.poolSize += count.load();
        }
    }

    stats.hitRate = hitCount_.load();
    stats.missRate = missCount_.load();

    return stats;
}

void MemoryPool::cleanup() {
    // 这里可以实现定期清理逻辑
    // 比如清理长时间未使用的内存块
    // 为了简化，暂时不实现复杂的清理逻辑
}

// ============================================================================
// NodeQueues 实现  
// ============================================================================

NodeQueues::NodeQueues() : memoryPool_(1024, 10000) {
    // 构造函数为空，池子初始为空
}

NodeQueues::~NodeQueues() {
    // 清理MessageNode池子
    MessageNode* msgNode = nullptr;
    while (messageNodePool_.try_dequeue(msgNode)) {
        if (msgNode) {
            delete msgNode;
        }
    }

    // 清理SendNode池子
    SendNode* sendNode = nullptr;
    while (sendNodePool_.try_dequeue(sendNode)) {
        if (sendNode) {
            delete sendNode;
        }
    }
}

MessageNode* NodeQueues::acquireMessageNode(int64_t headLength) {
    MessageNode* node = nullptr;

    // 尝试从池子获取
    if (messageNodePool_.try_dequeue(node) && node != nullptr) {
        messageNodePoolSize_.fetch_sub(1);
        messageNodeHitCount_.fetch_add(1);

        // 重置节点状态
        resetMessageNode(node);
        node->headLength = static_cast<short>(headLength);

        return node;
    }

    // 池子为空，创建新节点
    messageNodeMissCount_.fetch_add(1);
    totalMessageNodesCreated_.fetch_add(1);

    try {
        return new MessageNode(headLength);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create MessageNode: " << e.what() << std::endl;
        return nullptr;
    }
}

SendNode* NodeQueues::acquireSendNode(const char* msg, int64_t max_length, short msgid) {
    SendNode* node = nullptr;

    // 尝试从池子获取
    if (sendNodePool_.try_dequeue(node) && node != nullptr) {
        sendNodePoolSize_.fetch_sub(1);
        sendNodeHitCount_.fetch_add(1);

        // 重置节点状态并设置新数据
        resetSendNode(node);
        if (!node->safeSetSendNode(msg, max_length, msgid)) {
            // 设置失败，重新放回池子或删除
            releaseSendNode(node);
            return nullptr;
        }

        return node;
    }

    // 池子为空，创建新节点
    sendNodeMissCount_.fetch_add(1);
    totalSendNodesCreated_.fetch_add(1);

    try {
        return new SendNode(msg, max_length, msgid);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create SendNode: " << e.what() << std::endl;
        return nullptr;
    }
}

void NodeQueues::releaseMessageNode(MessageNode* node) {
    if (!node) {
        return;
    }

    // 验证节点是否可以安全回收
    if (!isNodeSafeToRecycle(node)) {
        delete node;
        return;
    }

    // 检查池子大小限制
    if (messageNodePoolSize_.load() >= MAX_MESSAGE_NODE_POOL_SIZE) {
        delete node;
        return;
    }

    // 清理节点状态
    resetMessageNode(node);

    // 尝试放回池子
    if (messageNodePool_.enqueue(node)) {
        messageNodePoolSize_.fetch_add(1);
    }
    else {
        // 放回失败，直接删除
        delete node;
    }
}

void NodeQueues::releaseSendNode(SendNode* node) {
    if (!node) {
        return;
    }

    // 验证节点是否可以安全回收
    if (!isNodeSafeToRecycle(node)) {
        delete node;
        return;
    }

    // 检查池子大小限制
    if (sendNodePoolSize_.load() >= MAX_SEND_NODE_POOL_SIZE) {
        delete node;
        return;
    }

    // 清理节点状态
    resetSendNode(node);

    // 尝试放回池子
    if (sendNodePool_.enqueue(node)) {
        sendNodePoolSize_.fetch_add(1);
    }
    else {
        // 放回失败，直接删除
        delete node;
    }
}

void NodeQueues::resetMessageNode(MessageNode* node) {
    if (!node) {
        return;
    }

    // 🔧 根据内存来源正确释放数据
    if (node->data) {
        if (node->dataSource == MemorySource::MEMORY_POOL) {
            // 尝试返回内存池
            if (!memoryPool_.deallocate(node->data, node->bufferSize)) {
                // 返回失败，直接释放
                delete[] node->data;
            }
        }
        else {
            // 普通new[]分配的内存
            delete[] node->data;
        }
        node->data = nullptr;
    }

    // 重置其他成员
    node->id = 0;
    node->length = 0;
    node->bufferSize = 0;
    node->headLength = 0;
    node->dataSource = MemorySource::NORMAL_NEW;

    // 清理session引用
    if (node->session) {
        node->session.reset();
    }
}

void NodeQueues::resetSendNode(SendNode* node) {
    if (!node) {
        return;
    }

    // 调用基类重置
    resetMessageNode(node);
}

bool NodeQueues::isNodeSafeToRecycle(MessageNode* node) const {
    if (!node) {
        return false;
    }

    // 检查session引用计数
    if (node->session && node->session.use_count() > 1) {
        return false; // 还有其他地方在使用这个session
    }

    // 其他安全检查可以在这里添加
    return true;
}

NodeQueues::NodePoolStats NodeQueues::getStats() const {
    NodePoolStats stats;

    stats.messageNodePoolSize = messageNodePoolSize_.load();
    stats.sendNodePoolSize = sendNodePoolSize_.load();
    stats.messageNodeHitRate = messageNodeHitCount_.load();
    stats.sendNodeHitRate = sendNodeHitCount_.load();
    stats.totalMessageNodesCreated = totalMessageNodesCreated_.load();
    stats.totalSendNodesCreated = totalSendNodesCreated_.load();

    return stats;
}

void NodeQueues::cleanup() {
    // 定期清理池子中的节点
    // 这里可以实现更复杂的清理逻辑
    // 比如限制池子中节点的存活时间等

    // 清理内存池
    memoryPool_.cleanup();
}