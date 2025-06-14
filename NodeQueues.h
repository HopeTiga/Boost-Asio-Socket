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

// �ڴ��ṹ
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

// �ڴ���� - �̰߳�ȫ���ڴ����
class MemoryPool {
public:
    MemoryPool(size_t initialBlockSize = 1024, size_t maxPoolSize = 10000);
    ~MemoryPool();

    // �����ڴ�
    void* allocate(size_t size);

    // �ͷ��ڴ�س��ӣ�ʧ����ֱ��delete
    bool deallocate(void* ptr, size_t size);

    // ��ȡ����ͳ����Ϣ
    struct PoolStats {
        size_t totalAllocated;
        size_t poolSize;
        size_t hitRate;
        size_t missRate;
    };
    PoolStats getStats() const;

    // ��������ڴ�飨���ڵ��ã�
    void cleanup();

private:
    // ����С������ڴ��
    std::unordered_map<size_t, moodycamel::ConcurrentQueue<void*>> sizePools;

    // ���Ӵ�С����
    std::unordered_map<size_t, std::atomic<size_t>> poolSizes;

    const size_t maxPoolSize_;
    const size_t initialBlockSize_;

    // ͳ����Ϣ
    mutable std::atomic<size_t> totalAllocated_{ 0 };
    mutable std::atomic<size_t> hitCount_{ 0 };
    mutable std::atomic<size_t> missCount_{ 0 };

    // ����sizePools�Ķ�д��
    mutable std::shared_mutex poolsMutex_;

    // �ڴ���븨������
    static size_t alignSize(size_t size);

    // ��ƽ̨�����ڴ����
    static void* allocateAligned(size_t size, size_t alignment);
    static void freeAligned(void* ptr);

    // ��ȡ�򴴽�ָ����С�ĳ���
    moodycamel::ConcurrentQueue<void*>& getOrCreatePool(size_t size);
};

// �ڵ���� - ����MessageNode��SendNode
class NodeQueues {


public:
    ~NodeQueues();

    static NodeQueues* getInstance() {
        static NodeQueues nodeQueues;
        return &nodeQueues;
    }

    // ��ȡMessageNode�����ȴӳ���ȡ�������½�
    MessageNode* acquireMessageNode(int64_t headLength = HEAD_TOTAL_LEN);

    // ��ȡSendNode�����ȴӳ���ȡ�������½�  
    SendNode* acquireSendNode(const char* msg, int64_t max_length, short msgid);

    // �ͷ�MessageNode�����Ȼس��ӣ�ʧ����ɾ��
    void releaseMessageNode(MessageNode* node);

    // �ͷ�SendNode�����Ȼس��ӣ�ʧ����ɾ��
    void releaseSendNode(SendNode* node);

    // ��ȡ����ͳ����Ϣ
    struct NodePoolStats {
        size_t messageNodePoolSize;
        size_t sendNodePoolSize;
        size_t messageNodeHitRate;
        size_t sendNodeHitRate;
        size_t totalMessageNodesCreated;
        size_t totalSendNodesCreated;
    };
    NodePoolStats getStats() const;

    // ��������еĹ��ڽڵ�
    void cleanup();

    // ��ȡ�ڴ��ʵ��
    MemoryPool& getMemoryPool() { return memoryPool_; }

private:
    NodeQueues();

    // ���ÿ����͸�ֵ
    NodeQueues(const NodeQueues&) = delete;
    NodeQueues& operator=(const NodeQueues&) = delete;

    // MessageNode����
    moodycamel::ConcurrentQueue<MessageNode*> messageNodePool_;

    // SendNode����  
    moodycamel::ConcurrentQueue<SendNode*> sendNodePool_;

    // ���Ӵ�С����
    static constexpr size_t MAX_MESSAGE_NODE_POOL_SIZE = 5000;
    static constexpr size_t MAX_SEND_NODE_POOL_SIZE = 5000;

    // ��ǰ���Ӵ�С������
    std::atomic<size_t> messageNodePoolSize_{ 0 };
    std::atomic<size_t> sendNodePoolSize_{ 0 };

    // ͳ����Ϣ
    mutable std::atomic<size_t> messageNodeHitCount_{ 0 };
    mutable std::atomic<size_t> messageNodeMissCount_{ 0 };
    mutable std::atomic<size_t> sendNodeHitCount_{ 0 };
    mutable std::atomic<size_t> sendNodeMissCount_{ 0 };
    mutable std::atomic<size_t> totalMessageNodesCreated_{ 0 };
    mutable std::atomic<size_t> totalSendNodesCreated_{ 0 };

    // �ڴ��ʵ��
    MemoryPool memoryPool_;

    // ����MessageNode״̬
    void resetMessageNode(MessageNode* node);

    // ����SendNode״̬  
    void resetSendNode(SendNode* node);

    // ��֤�ڵ��Ƿ���԰�ȫ����
    bool isNodeSafeToRecycle(MessageNode* node) const;
};