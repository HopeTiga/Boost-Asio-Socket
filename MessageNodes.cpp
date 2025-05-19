#include "MessageNodes.h"
#include "BufferPool.h"
#include "FastMemcpy_Avx.h"

MessageNode::MessageNode(int64_t headLength) :headLength(headLength), length(0), data(nullptr), fromPool(false) {
    // data 指针初始化为 nullptr
}

MessageNode::~MessageNode() {
    if (fromPool && data) {
        BufferPool::getInstance()->releaseBuffer(data);
        data = nullptr;
    } else if (data) {
        delete[] data;
        data = nullptr;
    }
}

void MessageNode::clear() {
    if (fromPool && data) {
        BufferPool::getInstance()->releaseBuffer(data);
        data = nullptr;
    } else if (data) {
        delete[] data;
        data = nullptr;
    }
    length = 0;
    session.reset();
    fromPool = false;
}

MessageNode::MessageNode(std::shared_ptr<CSession> session, short id, char* datas, int64_t length, short headLength)
    : session(session), id(id), length(length), headLength(headLength), fromPool(false) {
    data = new char[length + 1];
    memcpy(data, datas, length);
    data[length] = '\0';
}

void MessageNode::setLength(size_t length) {
    if (data && !fromPool) {
        delete[] data;
    }
    this->length = length;
}


SendNode::SendNode(const char* msg, int64_t max_length, short msgid) : MessageNode(HEAD_TOTAL_LEN) {
    short msgids = boost::asio::detail::socket_ops::host_to_network_short(msgid);
    int64_t max_lengths = boost::asio::detail::socket_ops::host_to_network_long(max_length);

    this->length = max_length + HEAD_TOTAL_LEN;
    data = new char[this->length];
    smart_memcpy(data, &msgids, HEAD_ID_LEN);
    smart_memcpy(data + HEAD_ID_LEN, &max_lengths, HEAD_DATA_LEN);
    smart_memcpy(data + HEAD_TOTAL_LEN, msg, max_length);
}

void SendNode::setSendNode(const char* msg, int64_t max_length, short msgid) {
    short msgids = boost::asio::detail::socket_ops::host_to_network_short(msgid);
    int64_t max_lengths = boost::asio::detail::socket_ops::host_to_network_long(max_length);
    if (data && !fromPool) {
        delete[] data;
    }
    this->length = max_length + HEAD_TOTAL_LEN;
    data = new char[this->length];
    smart_memcpy(data, &msgids, HEAD_ID_LEN);
    smart_memcpy(data + HEAD_ID_LEN, &max_lengths, HEAD_DATA_LEN);
    smart_memcpy(data + HEAD_TOTAL_LEN, msg, max_length);
}

SendNode::~SendNode() {

}


NodeQueues::~NodeQueues()
{
	
	MessageNode* messageNode;

	while (messageQueues.pop(messageNode)) {
	
		if (messageNode != nullptr) {
		
			delete messageNode;

			messageNode = nullptr;
			
		}
	}


	SendNode* sendNode;

	while (sendQueues.pop(sendNode)) {

		sendQueues.pop(sendNode);

		if (sendNode != nullptr) {

			delete sendNode;

			sendNode = nullptr;

		}
	}

}

NodeQueues::NodeQueues(size_t size):messageQueues(size),sendQueues(size)
{

}

bool NodeQueues::releaseMessageNode(MessageNode* node)
{
    if (!node->fromPool) return false;

	node->clear();

	return messageQueues.push(node);

}

bool NodeQueues::releaseSendNode(SendNode* node)
{
    if (!node->fromPool) return false;

	node->clear();

	return sendQueues.push(node);;
}

bool NodeQueues::getMessageNode(MessageNode*& node)
{
	return messageQueues.pop(node);
}

bool NodeQueues::getSendNode(SendNode*& node)
{
	return sendQueues.pop(node);;
}
