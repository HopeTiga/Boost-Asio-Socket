#pragma once
#include "const.h"
#include <boost/lockfree/queue.hpp>

extern class CSession;

class MessageNode {

public:
	MessageNode(int64_t headLength);

	MessageNode(std::shared_ptr<CSession> session, short id, char* data, int64_t length, short headLength);

	virtual ~MessageNode();

	void clear();

	short headLength;

	short id;

	char* data; // 直接使用 char* 指针

	int64_t length;

	void setLength(size_t length);

	std::shared_ptr<CSession> session;

	bool fromPool; // 标记 data 是否来自 BufferPool
};


class SendNode : public MessageNode {
public:

	SendNode(const char* msg, int64_t max_len, short msg_id);  // 修改参数类型

	~SendNode();

	void setSendNode(const char* msg, int64_t max_len, short msg_id);  // 修改参数类型

	short id;


};

class NodeQueues {

public:

	~NodeQueues();

	static NodeQueues* getInstantce() {
		static NodeQueues nodeQueues;
		return &nodeQueues;
	}

	bool releaseMessageNode(MessageNode*  node);

	bool releaseSendNode(SendNode* node);

	bool getMessageNode(MessageNode * node);

	bool getSendNode(SendNode* node);

private:

	boost::lockfree::queue<MessageNode*> messageQueues;

	boost::lockfree::queue<SendNode*> sendQueues;


};