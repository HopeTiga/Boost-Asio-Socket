#include "NodeQueues.h"
#include "FastMemcpy_Avx.h"
#include <algorithm>
#include <chrono>

// ============================================================================
// 跨平台内存对齐分配函数 - 优化版本
// ============================================================================

void* MemoryPool::allocateAligned(size_t size) {
    // 🔧 优化：智能对齐策略
    size_t alignment = 8; // 默认8字节对齐，足够大多数情况
    if (size >= 1024) {
        alignment = 32; // 大内存块使用32字节对齐
    }

#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
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
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ============================================================================
// MemoryPool 优化实现
// ============================================================================

MemoryPool::MemoryPool(size_t initialBlockSize, size_t maxPoolSize)
    : maxPoolSizePerType_(maxPoolSize) {

    // 🔧 优化：初始化数组中的原子变量
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        poolSizes_[i].store(0);
    }
}

MemoryPool::~MemoryPool() {
    // 🔧 优化：清理所有池子中的内存
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        void* ptr = nullptr;
        while (sizePools_[i].try_dequeue(ptr)) {
            if (ptr) {
                freeAligned(ptr);
            }
        }
    }
}

size_t MemoryPool::getBestPoolIndex(size_t size) const {
    // 🔧 优化：使用二分查找或线性查找找到最适合的池子
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        if (size <= POOL_SIZES[i]) {
            return i;
        }
    }
    return POOL_COUNT; // 表示没有合适的池子
}

size_t MemoryPool::getBestPoolSize(size_t size) const {
    size_t index = getBestPoolIndex(size);
    return (index < POOL_COUNT) ? POOL_SIZES[index] : size;
}

bool MemoryPool::shouldRejectAllocation(size_t size) const {
    // 🔧 优化：更严格的分配检查
    if (size > MAX_BLOCK_SIZE) {
        return true;
    }

    if (totalMemoryUsed_.load() + size > MAX_TOTAL_MEMORY) {
        return true;
    }

    return false;
}

bool MemoryPool::shouldRejectDeallocation(size_t size, size_t poolIndex) const {
    // 🔧 优化：更精确的回收检查
    if (size > MAX_BLOCK_SIZE) {
        return true;
    }

    if (poolIndex >= POOL_COUNT) {
        return true;
    }

    // 检查该类型池子是否已满
    if (poolSizes_[poolIndex].load() >= MAX_POOL_SIZE_PER_TYPE) {
        return true;
    }

    // 检查总池子内存是否过多
    if (totalPoolMemory_.load() > MAX_TOTAL_MEMORY / 2) {
        return true;
    }

    return false;
}

size_t MemoryPool::alignSize(size_t size) {
    // 🔧 优化：简化对齐逻辑
    return (size + 7) & ~7; // 8字节对齐
}

void* MemoryPool::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    // 🔧 优化：直接检查是否应该拒绝分配
    if (shouldRejectAllocation(size)) {
        // 直接分配，不使用池子
        void* ptr = allocateAligned(size);
        if (ptr) {
            missCount_.fetch_add(1);
            totalMemoryUsed_.fetch_add(size);
        }
        return ptr;
    }

    // 🔧 优化：找到最适合的池子
    size_t poolIndex = getBestPoolIndex(size);

    if (poolIndex >= POOL_COUNT) {
        // 没有合适的池子，直接分配
        void* ptr = allocateAligned(size);
        if (ptr) {
            missCount_.fetch_add(1);
            totalMemoryUsed_.fetch_add(size);
        }
        return ptr;
    }

    size_t poolSize = POOL_SIZES[poolIndex];

    // 🔧 优化：尝试从池子获取
    void* ptr = nullptr;
    if (sizePools_[poolIndex].try_dequeue(ptr) && ptr != nullptr) {
        poolSizes_[poolIndex].fetch_sub(1);
        totalPoolMemory_.fetch_sub(poolSize);
        hitCount_.fetch_add(1);
        return ptr;
    }

    // 🔧 优化：池子中没有，分配新内存
    ptr = allocateAligned(poolSize);
    if (ptr) {
        totalAllocated_.fetch_add(1);
        totalMemoryUsed_.fetch_add(poolSize);
        missCount_.fetch_add(1);
    }

    return ptr;
}

bool MemoryPool::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return false;
    }

    // 🔧 优化：找到对应的池子
    size_t poolIndex = getBestPoolIndex(size);
    size_t poolSize = (poolIndex < POOL_COUNT) ? POOL_SIZES[poolIndex] : size;

    // 🔧 优化：检查是否应该拒绝回收
    if (shouldRejectDeallocation(size, poolIndex)) {
        freeAligned(ptr);
        totalMemoryUsed_.fetch_sub(poolSize);
        return false;
    }

    // 🔧 优化：尝试放回对应的池子
    if (poolIndex < POOL_COUNT && sizePools_[poolIndex].enqueue(ptr)) {
        poolSizes_[poolIndex].fetch_add(1);
        totalPoolMemory_.fetch_add(poolSize);
        // 注意：这里不减少totalMemoryUsed_，因为内存仍在池子中
        return true;
    }

    // 🔧 优化：放回失败，直接释放
    freeAligned(ptr);
    totalMemoryUsed_.fetch_sub(poolSize);
    return false;
}

MemoryPool::PoolStats MemoryPool::getStats() const {
    PoolStats stats;
    stats.totalAllocated = totalAllocated_.load();

    // 🔧 优化：计算所有池子的总大小
    stats.poolSize = 0;
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        stats.poolSize += poolSizes_[i].load();
    }

    stats.hitRate = hitCount_.load();
    stats.missRate = missCount_.load();

    return stats;
}

void MemoryPool::cleanup() {
    // 🔧 优化：定期清理策略
    constexpr size_t CLEANUP_THRESHOLD = MAX_POOL_SIZE_PER_TYPE / 2;

    for (size_t i = 0; i < POOL_COUNT; ++i) {
        size_t currentSize = poolSizes_[i].load();

        // 如果池子太大，清理一些内存
        if (currentSize > CLEANUP_THRESHOLD) {
            size_t cleanupCount = currentSize - CLEANUP_THRESHOLD;

            for (size_t j = 0; j < cleanupCount; ++j) {
                void* ptr = nullptr;
                if (sizePools_[i].try_dequeue(ptr) && ptr != nullptr) {
                    freeAligned(ptr);
                    poolSizes_[i].fetch_sub(1);
                    totalPoolMemory_.fetch_sub(POOL_SIZES[i]);
                    totalMemoryUsed_.fetch_sub(POOL_SIZES[i]);
                }
                else {
                    break; // 池子空了
                }
            }
        }
    }
}

// ============================================================================
// NodeQueues 优化实现  
// ============================================================================

NodeQueues::NodeQueues() {
    // 🔧 优化：构造函数保持简单
}

NodeQueues::~NodeQueues() {
    // 🔧 优化：清理MessageNode池子
    MessageNode* msgNode = nullptr;
    while (messageNodePool_.try_dequeue(msgNode)) {
        if (msgNode) {
            delete msgNode;
        }
    }

    // 🔧 优化：清理SendNode池子
    SendNode* sendNode = nullptr;
    while (sendNodePool_.try_dequeue(sendNode)) {
        if (sendNode) {
            delete sendNode;
        }
    }
}

MessageNode* NodeQueues::acquireMessageNode(int64_t headLength) {
    MessageNode* node = nullptr;

    // 🔧 优化：尝试从池子获取
    if (messageNodePool_.try_dequeue(node) && node != nullptr) {
        messageNodePoolSize_.fetch_sub(1);
        messageNodeHitCount_.fetch_add(1);

        // 重置节点状态
        resetMessageNode(node);
        node->headLength = static_cast<short>(headLength);

        return node;
    }

    // 🔧 优化：池子为空，创建新节点
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

    // 🔧 优化：尝试从池子获取
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

    // 🔧 优化：池子为空，创建新节点
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

    // 🔧 优化：验证节点是否可以安全回收
    if (!isNodeSafeToRecycle(node)) {
        delete node;
        return;
    }

    // 🔧 优化：检查池子大小限制
    if (messageNodePoolSize_.load() >= MAX_MESSAGE_NODE_POOL_SIZE) {
        delete node;
        return;
    }

    // 🔧 优化：清理节点状态
    resetMessageNode(node);

    // 🔧 优化：尝试放回池子
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

    // 🔧 优化：验证节点是否可以安全回收
    if (!isNodeSafeToRecycle(node)) {
        delete node;
        return;
    }

    // 🔧 优化：检查池子大小限制
    if (sendNodePoolSize_.load() >= MAX_SEND_NODE_POOL_SIZE) {
        delete node;
        return;
    }

    // 🔧 优化：清理节点状态
    resetSendNode(node);

    // 🔧 优化：尝试放回池子
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

    // 🔧 优化：根据内存来源正确释放数据
    if (node->data) {
        if (node->dataSource == MemorySource::MEMORY_POOL) {
            // 🔧 优化：尝试返回内存池，使用正确的大小
            if (!MemoryPool::getInstance()->deallocate(node->data, node->bufferSize)) {
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

    // 🔧 优化：重置所有成员
    node->id = 0;
    node->length = 0;
    node->bufferSize = 0;
    node->headLength = 0;
    node->dataSource = MemorySource::NORMAL_NEW;

    // 🔧 优化：清理session引用
    if (node->session) {
        node->session.reset();
    }
}

void NodeQueues::resetSendNode(SendNode* node) {
    if (!node) {
        return;
    }

    // 🔧 优化：调用基类重置
    resetMessageNode(node);
}

bool NodeQueues::isNodeSafeToRecycle(MessageNode* node) const {
    if (!node) {
        return false;
    }

    // 🔧 优化：检查session引用计数
    if (node->session && node->session.use_count() > 1) {
        return false; // 还有其他地方在使用这个session
    }

    // 🔧 优化：其他安全检查
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
    // 🔧 优化：定期清理池子中的节点
    constexpr size_t CLEANUP_MSG_THRESHOLD = MAX_MESSAGE_NODE_POOL_SIZE / 2;
    constexpr size_t CLEANUP_SEND_THRESHOLD = MAX_SEND_NODE_POOL_SIZE / 2;

    // 清理MessageNode池子
    size_t msgPoolSize = messageNodePoolSize_.load();
    if (msgPoolSize > CLEANUP_MSG_THRESHOLD) {
        size_t cleanupCount = msgPoolSize - CLEANUP_MSG_THRESHOLD;

        for (size_t i = 0; i < cleanupCount; ++i) {
            MessageNode* node = nullptr;
            if (messageNodePool_.try_dequeue(node) && node != nullptr) {
                delete node;
                messageNodePoolSize_.fetch_sub(1);
            }
            else {
                break;
            }
        }
    }

    // 清理SendNode池子
    size_t sendPoolSize = sendNodePoolSize_.load();
    if (sendPoolSize > CLEANUP_SEND_THRESHOLD) {
        size_t cleanupCount = sendPoolSize - CLEANUP_SEND_THRESHOLD;

        for (size_t i = 0; i < cleanupCount; ++i) {
            SendNode* node = nullptr;
            if (sendNodePool_.try_dequeue(node) && node != nullptr) {
                delete node;
                sendNodePoolSize_.fetch_sub(1);
            }
            else {
                break;
            }
        }
    }

    // 🔧 优化：清理内存池
    MemoryPool::getInstance()->cleanup();
}