#pragma once
#include "const.h"
#include <mutex>
#include <atomic>
#include <cassert>

extern class CSession;

enum class MemorySource {
    NORMAL_NEW,    // 普通new[]分配
    MEMORY_POOL    // 内存池分配
};

class MessageNode {
public:
    MessageNode(int64_t headLength);
    MessageNode(std::shared_ptr<CSession> session, short id, char* data, int64_t length, short headLength);
    virtual ~MessageNode();

    virtual void clear();

    // 数据成员
    short headLength;
    short id;
    char* data;
    int64_t length;
    size_t bufferSize;
    std::shared_ptr<CSession> session;

    // 🔧 新增：内存来源标记
    MemorySource dataSource = MemorySource::NORMAL_NEW;
};

class SendNode : public MessageNode {
public:
    SendNode(const char* msg, int64_t max_len, short msg_id);
    ~SendNode();

    void setSendNode(const char* msg, int64_t max_len, short msg_id);
    virtual void clear() override;

    // 线程安全的设置方法
    bool safeSetSendNode(const char* msg, int64_t max_length, short msgid);
};