#include "NodeQueues.h"
#include "FastMemcpy_Avx.h"
#include <algorithm>
#include <chrono>
#include <iostream>

// ============================================================================
// MemoryBlock 实现
// ============================================================================

MemoryBlock::MemoryBlock() : ptr(nullptr), size(0), inUse(false) {}

MemoryBlock::MemoryBlock(void* p, size_t s)
    : ptr(p), size(s), inUse(true), allocTime(std::chrono::steady_clock::now()) {
}

// ============================================================================
// MemoryPool 实现
// ============================================================================

MemoryPool::MemoryPool(size_t initialBlockSize, size_t maxPoolSize)
    : maxPoolSizePerType_(maxPoolSize) {
    // 初始化数组中的原子变量
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        poolSizes_[i].store(0);
    }
}

MemoryPool::~MemoryPool() {
    // 清理所有池子中的内存
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        void* ptr = nullptr;
        while (sizePools_[i].try_dequeue(ptr)) {
            if (ptr) {
                freeAligned(ptr);
            }
        }
    }
}

void* MemoryPool::allocateAligned(size_t size) {
    return malloc(size);
}

void MemoryPool::freeAligned(void* ptr) {
    if (!ptr) return;
    free(ptr);
}

size_t MemoryPool::alignSize(size_t size) {
    // 简化：不再进行内存对齐，直接返回原始大小
    return size;
}

size_t MemoryPool::getBestPoolIndex(size_t size) const {
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
    if (size > MAX_BLOCK_SIZE) {
        return true;
    }
    if (totalMemoryUsed_.load() + size > MAX_TOTAL_MEMORY) {
        return true;
    }
    return false;
}

bool MemoryPool::shouldRejectDeallocation(size_t size, size_t poolIndex) const {
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

void* MemoryPool::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    // 检查是否应该拒绝分配
    if (shouldRejectAllocation(size)) {
        // 直接分配，不使用池子
        void* ptr = allocateAligned(size);
        if (ptr) {
            missCount_.fetch_add(1);
            totalMemoryUsed_.fetch_add(size);
        }
        return ptr;
    }

    // 找到最适合的池子
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

    // 尝试从池子获取
    void* ptr = nullptr;
    if (sizePools_[poolIndex].try_dequeue(ptr) && ptr != nullptr) {
        poolSizes_[poolIndex].fetch_sub(1);
        totalPoolMemory_.fetch_sub(poolSize);
        hitCount_.fetch_add(1);

        // 清零内存但只清理实际需要的大小
        std::memset(ptr, 0, size);
        return ptr;
    }

    // 池子中没有，分配新内存
    ptr = allocateAligned(poolSize);
    if (ptr) {
        totalAllocated_.fetch_add(1);
        totalMemoryUsed_.fetch_add(poolSize);
        missCount_.fetch_add(1);

        // 清零内存但只清理实际需要的大小
        std::memset(ptr, 0, size);
    }

    return ptr;
}

bool MemoryPool::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return false;
    }

    // 找到对应的池子
    size_t poolIndex = getBestPoolIndex(size);
    size_t poolSize = (poolIndex < POOL_COUNT) ? POOL_SIZES[poolIndex] : size;

    // 检查是否应该拒绝回收
    if (shouldRejectDeallocation(size, poolIndex)) {
        freeAligned(ptr);
        totalMemoryUsed_.fetch_sub(poolSize);
        return false;
    }

    // 在放回池子前清零内存，防止数据泄漏
    std::memset(ptr, 0, poolSize);

    // 尝试放回对应的池子
    if (poolIndex < POOL_COUNT && sizePools_[poolIndex].enqueue(ptr)) {
        poolSizes_[poolIndex].fetch_add(1);
        totalPoolMemory_.fetch_add(poolSize);
        return true;
    }

    // 放回失败，直接释放
    freeAligned(ptr);
    totalMemoryUsed_.fetch_sub(poolSize);
    return false;
}

MemoryPool::PoolStats MemoryPool::getStats() const {
    PoolStats stats;
    stats.totalAllocated = totalAllocated_.load();

    // 计算所有池子的总大小
    stats.poolSize = 0;
    for (size_t i = 0; i < POOL_COUNT; ++i) {
        stats.poolSize += poolSizes_[i].load();
    }

    stats.hitRate = hitCount_.load();
    stats.missRate = missCount_.load();

    return stats;
}

void MemoryPool::cleanup() {
    // 定期清理策略
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
// NodeQueues 实现（使用自定义删除器）
// ============================================================================

NodeQueues::NodeQueues() = default;

NodeQueues::~NodeQueues() {
    // 清理池子中的所有对象
    MessageNode* msgNode = nullptr;
    while (messageNodePool_.try_dequeue(msgNode)) {
        delete msgNode;
    }

    SendNode* sendNode = nullptr;
    while (sendNodePool_.try_dequeue(sendNode)) {
        delete sendNode;
    }
}

std::shared_ptr<MessageNode> NodeQueues::acquireMessageNode(int64_t headLength) {
    // 尝试从池子获取
    MessageNode* rawNode = nullptr;
    if (messageNodePool_.try_dequeue(rawNode) && rawNode != nullptr) {
        messageNodePoolSize_.fetch_sub(1);
        messageNodeHitCount_.fetch_add(1);

        // 重置节点状态
        resetMessageNode(rawNode);
        rawNode->headLength = static_cast<short>(headLength);

        // 关键：使用自定义删除器，将对象返回池子而不是删除
        return std::shared_ptr<MessageNode>(rawNode, [this](MessageNode* node) {
            this->returnMessageNodeToPool(node);
            });
    }

    // 池子为空，创建新节点
    messageNodeMissCount_.fetch_add(1);
    totalMessageNodesCreated_.fetch_add(1);

    try {
        MessageNode* newNode = new MessageNode(headLength);

        // 关键：新节点也使用自定义删除器
        return std::shared_ptr<MessageNode>(newNode, [this](MessageNode* node) {
            this->returnMessageNodeToPool(node);
            });
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create MessageNode: " << e.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<SendNode> NodeQueues::acquireSendNode(const char* msg, int64_t max_length, short msgid) {
    // 尝试从池子获取
    SendNode* rawNode = nullptr;
    if (sendNodePool_.try_dequeue(rawNode) && rawNode != nullptr) {
        sendNodePoolSize_.fetch_sub(1);
        sendNodeHitCount_.fetch_add(1);

        // 重置节点状态并设置新数据
        resetSendNode(rawNode);
        if (!rawNode->safeSetSendNode(msg, max_length, msgid)) {
            // 设置失败，直接放回池子
            returnSendNodeToPool(rawNode);
            return nullptr;
        }

        // 关键：使用自定义删除器
        return std::shared_ptr<SendNode>(rawNode, [this](SendNode* node) {
            this->returnSendNodeToPool(node);
            });
    }

    // 池子为空，创建新节点
    sendNodeMissCount_.fetch_add(1);
    totalSendNodesCreated_.fetch_add(1);

    try {
        SendNode* newNode = new SendNode(msg, max_length, msgid);

        // 关键：新节点也使用自定义删除器
        return std::shared_ptr<SendNode>(newNode, [this](SendNode* node) {
            this->returnSendNodeToPool(node);
            });
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create SendNode: " << e.what() << std::endl;
        return nullptr;
    }
}

void NodeQueues::releaseMessageNode(std::shared_ptr<MessageNode> node) {
    // 空方法，自定义删除器会自动处理
}

void NodeQueues::releaseSendNode(std::shared_ptr<SendNode> node) {
    // 空方法，自定义删除器会自动处理
}

void NodeQueues::returnMessageNodeToPool(MessageNode* node) {
    if (!node) return;

    // 检查池子大小限制
    if (messageNodePoolSize_.load() >= MAX_MESSAGE_NODE_POOL_SIZE) {
        delete node;  // 池子满了，直接删除
        return;
    }

    // 重置节点状态（但不删除data，让clear()处理）
    node->id = 0;
    node->length = 0;
    node->headLength = 0;
    if (node->session) {
        node->session.reset();
    }

    // 放回池子
    if (messageNodePool_.enqueue(node)) {
        messageNodePoolSize_.fetch_add(1);
    }
    else {
        delete node;  // 放回失败，删除
    }
}

void NodeQueues::returnSendNodeToPool(SendNode* node) {
    if (!node) return;

    // 检查池子大小限制
    if (sendNodePoolSize_.load() >= MAX_SEND_NODE_POOL_SIZE) {
        delete node;  // 池子满了，直接删除
        return;
    }

    // 重置节点状态
    node->id = 0;
    node->length = 0;
    node->headLength = 0;
    if (node->session) {
        node->session.reset();
    }
    // data会在下次使用时被safeSetSendNode重置

    // 放回池子
    if (sendNodePool_.enqueue(node)) {
        sendNodePoolSize_.fetch_add(1);
    }
    else {
        delete node;  // 放回失败，删除
    }
}

void NodeQueues::resetMessageNode(MessageNode* node) {
    if (!node) return;

    // 只重置基本状态，data由使用者设置
    node->id = 0;
    node->length = 0;
    node->headLength = 0;
    node->dataSource = MemorySource::NORMAL_NEW;

    if (node->session) {
        node->session.reset();
    }

    // 不清理data，让使用者自己管理
}

void NodeQueues::resetSendNode(SendNode* node) {
    if (!node) return;
    resetMessageNode(node);
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

    // 清理内存池
    MemoryPool::getInstance()->cleanup();
}