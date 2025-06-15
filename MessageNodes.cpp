#include "MessageNodes.h"
#include "FastMemcpy_Avx.h"
#include "NodeQueues.h"
#include <iostream>

MessageNode::MessageNode(int64_t headLength)
    : headLength(static_cast<short>(headLength)),
    id(0),
    data(nullptr),
    length(0),
    bufferSize(0),
    dataSource(MemorySource::NORMAL_NEW)
{
}

MessageNode::MessageNode(std::shared_ptr<CSession> session, short id, char* data, int64_t length, short headLength)
    : headLength(headLength),
    id(id),
    data(data),
    length(length),
    bufferSize(static_cast<size_t>(length)),
    session(session),
    dataSource(MemorySource::NORMAL_NEW)
{
}

MessageNode::~MessageNode() {
    clear();
}

void MessageNode::clear() {
    if (data) {
        // 🔧 根据内存来源正确释放
        if (dataSource == MemorySource::MEMORY_POOL) {
            if (!MemoryPool::getInstance()->deallocate(data, bufferSize)) {
                // 返回内存池失败，直接释放
                delete[] data;
            }
        }
        else {
            delete[] data;
        }
        data = nullptr;
    }

    // 重置所有状态
    length = 0;
    id = 0;
    dataSource = MemorySource::NORMAL_NEW;

    // 清理session引用
    if (session) {
        session.reset();
    }
}

SendNode::SendNode(const char* msg, int64_t max_length, short msgid)
    : MessageNode(HEAD_TOTAL_LEN) {
    if (msg != nullptr && max_length > 0) {
        safeSetSendNode(msg, max_length, msgid);
    }
}

SendNode::~SendNode() {
    // 基类析构函数会处理清理
}

bool SendNode::safeSetSendNode(const char* msg, int64_t max_length, short msgid) {
    if (!msg || max_length <= 0) {
        return false;
    }

    // 清理旧数据
    if (data) {
        // 🔧 根据内存来源正确释放旧数据
        if (dataSource == MemorySource::MEMORY_POOL) {
            if (!MemoryPool::getInstance()->deallocate(data, bufferSize)) {
                delete[] data;
            }
        }
        else {
            delete[] data;
        }
        data = nullptr;
    }

    // 准备数据
    uint16_t msgids = boost::asio::detail::socket_ops::host_to_network_short(
        static_cast<uint16_t>(msgid));
    uint64_t max_lengths = boost::asio::detail::socket_ops::host_to_network_long(
        static_cast<uint64_t>(max_length));

    this->id = msgid;
    this->length = max_length;
    size_t total_size = max_length + HEAD_TOTAL_LEN;
    bufferSize = total_size;

    data = static_cast<char*>(MemoryPool::getInstance()->allocate(total_size));

    if (data) {
        dataSource = MemorySource::MEMORY_POOL;
        bufferSize = MemoryPool::alignSize(total_size);
    }
    else {
        // 内存池分配失败，回退到普通分配
        data = new char[total_size];
        dataSource = MemorySource::NORMAL_NEW;
        if (!data) {
            return false;
        }
    }

    smart_memcpy(data, &msgids, HEAD_ID_LEN);
    smart_memcpy(data + HEAD_ID_LEN, &max_lengths, HEAD_DATA_LEN);
    smart_memcpy(data + HEAD_TOTAL_LEN, msg, max_length);

    return true;
}

void SendNode::setSendNode(const char* msg, int64_t max_length, short msgid) {
    safeSetSendNode(msg, max_length, msgid);
}

void SendNode::clear() {
    if (data) {
        // 🔧 根据内存来源正确释放
        if (dataSource == MemorySource::MEMORY_POOL) {
            if (!MemoryPool::getInstance()->deallocate(data, bufferSize)) {
                delete[] data;
            }
        }
        else {
            delete[] data;
        }
        data = nullptr;
    }

    // 重置所有状态
    length = 0;
    id = 0;
    dataSource = MemorySource::NORMAL_NEW;

    // 清理session引用
    if (session) {
        session.reset();
    }
}