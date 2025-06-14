#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"
#include <boost/uuid/uuid.hpp>            // uuid 类  
#include <boost/uuid/uuid_generators.hpp> // 生成器  
#include <boost/uuid/uuid_io.hpp>   
#include "SessionSendThread.h"
#include "FastMemcpy_Avx.h"
#include "NodeQueues.h"
#include <sstream>


CSession::CSession(boost::asio::io_context& ioContext, CServer* cserver) :socket(ioContext)
, context(ioContext), server(cserver), isStop(false) {

	boost::uuids::random_generator generator;

	boost::uuids::uuid uuids = generator();

	sessionID = boost::uuids::to_string(uuids);

	//node = new MessageNode(HEAD_TOTAL_LEN);
}

CSession::~CSession() {
    
}

// CSession.cpp 中 start() 方法的修改

void CSession::start() {
    auto self = shared_from_this();
    boost::asio::co_spawn(context, [self]() -> boost::asio::awaitable<void> {
        char* headerBuffer = nullptr;
        char* bodyBuffer = nullptr;
        int64_t bodyLength = 0;
        short msgId = 0;
        size_t headerSize = sizeof(short) + sizeof(int64_t);
        size_t bodySize = 0;

        try {
            while (!self->isStop) {
                // 接收消息头
                headerBuffer = new char[HEAD_TOTAL_LEN];
                if (!headerBuffer) {
                    headerBuffer = new char[headerSize];
                }

                size_t headerRead = 0;
                while (headerRead < headerSize) {
                    size_t n = co_await self->socket.async_read_some(
                        boost::asio::buffer(headerBuffer + headerRead, headerSize - headerRead),
                        boost::asio::use_awaitable);

                    if (n == 0) {
                        self->close();
                        co_return;
                    }
                    headerRead += n;
                }

                // 解析消息头
                short rawMsgId = 0;
                int64_t rawBodyLength = 0;
                memcpy(&rawMsgId, headerBuffer, sizeof(short));
                memcpy(&rawBodyLength, headerBuffer + sizeof(short), sizeof(int64_t));
                msgId = boost::asio::detail::socket_ops::network_to_host_short(rawMsgId);
                bodyLength = boost::asio::detail::socket_ops::network_to_host_long(rawBodyLength);

                if (headerBuffer) {
                    delete[] headerBuffer;
                    headerBuffer = nullptr;
                }

                // 验证消息体长度的合理性
                if (bodyLength <= 0 || bodyLength > 1024 * 1024) {  // 🔧 添加最大长度检查
                    std::cerr << "Invalid message body length: " << bodyLength << std::endl;
                    self->close();
                    co_return;
                }

                // 接收消息体
                bodySize = static_cast<size_t>(bodyLength);
                bodyBuffer = new char[bodySize];
                if (!bodyBuffer) {
                    std::cerr << "Failed to allocate body buffer of size: " << bodySize << std::endl;
                    self->close();
                    co_return;
                }

                size_t bodyRead = 0;
                while (bodyRead < bodySize) {
                    size_t n = co_await self->socket.async_read_some(
                        boost::asio::buffer(bodyBuffer + bodyRead, bodySize - bodyRead),
                        boost::asio::use_awaitable);

                    if (n == 0) {
                        self->close();
                        co_return;
                    }
                    bodyRead += n;
                }

                // 🔧 关键修复：正确的所有权转移和引用计数管理
                MessageNode* node = NodeQueues::getInstance()->acquireMessageNode(HEAD_TOTAL_LEN);
                // 设置节点数据 - 转移所有权
                node->data = bodyBuffer;
                node->id = msgId;
                node->length = bodyLength;
                node->bufferSize = bodySize;
                node->session = self;

                LogicSystem::getInstance()->postMessageToQueue(node);

                bodyBuffer = nullptr;  // 数据所有权已转移给node
                node = nullptr;        // 清空本地引用
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in CSession::start: " << e.what() << std::endl;
            self->close();
        }
        }, [self](std::exception_ptr p) {
            // 异常处理保持不变...
            if (p) {
                try {
                    std::rethrow_exception(p);
                }
                catch (const boost::system::system_error& e) {
                    std::cerr << "CSession unhandled coroutine (Boost.System error): "
                        << e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
                    if (self && !self->isStop) {
                        self->handleError(e.code(), "Unhandled Boost.System exception in start coroutine");
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "CSession unhandled coroutine (std::exception): " << e.what() << std::endl;
                    if (self && !self->isStop) {
                        self->handleError(boost::system::errc::make_error_code(boost::system::errc::owner_dead), "Unhandled std::exception in start coroutine");
                    }
                }
                catch (...) {
                    std::cerr << "CSession unhandled coroutine (unknown exception)." << std::endl;
                    if (self && !self->isStop) {
                        self->handleError(boost::system::errc::make_error_code(boost::system::errc::owner_dead), "Unhandled unknown exception in start coroutine");
                    }
                }
            }
            });
}


void CSession::send(char* msg, int64_t max_length, short msgid) {
    try {
        // 使用新的安全获取节点方法
        SendNode* nowNode = NodeQueues::getInstance()->acquireSendNode(msg, max_length, msgid);

        if (nowNode) {
            // 节点已经是干净的，直接设置数据
            //nowNode->setSendNode(msg, max_length, msgid);

            if (this->sendNodes.enqueue(nowNode)) {
                nowNode = nullptr;
            }

            auto self = shared_from_this();

            boost::asio::co_spawn(context, [self,nowNodes = nowNode]() mutable -> boost::asio::awaitable<void> {
                try {
                    if (nowNodes != nullptr) {

                        co_await boost::asio::async_write(self->socket,
                            boost::asio::buffer(nowNodes->data, nowNodes->length + HEAD_TOTAL_LEN),
                            boost::asio::use_awaitable);

                        NodeQueues::getInstance()->releaseSendNode(nowNodes);

                        nowNodes = nullptr;
                    }
                    // 处理队列中的其他消息
                    SendNode* queuedNode = nullptr;
                    while (self->sendNodes.try_dequeue(queuedNode)) {
                        if (queuedNode) {
                            co_await boost::asio::async_write(self->socket,
                                boost::asio::buffer(queuedNode->data, queuedNode->length + HEAD_TOTAL_LEN),
                                boost::asio::use_awaitable);

                            if (queuedNode) {
                                NodeQueues::getInstance()->releaseSendNode(queuedNode);
                            }
                            queuedNode = nullptr;
                        }
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "CSession::send error: " << e.what() << std::endl;
                    if (!self->isStop) {
                        self->close();
                    }
                }
                }, [self](std::exception_ptr p) {
                    if (p && !self->isStop) {
                        self->close();
                    }
                    });
        }
    }
    catch (std::exception& e) {
        std::cout << "CSession::send ERROR:" << e.what() << std::endl;
    }
}

void CSession::send(std::string msg, short msgid) {
    try {
        // 使用新的安全获取节点方法
        SendNode* nowNode = NodeQueues::getInstance()->acquireSendNode(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);

        if (nowNode) {
            // 节点已经是干净的，直接设置数据
            //nowNode->setSendNode(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);

            if (this->sendNodes.enqueue(nowNode)) {
                nowNode = nullptr;
            }

            auto self = shared_from_this();

            boost::asio::co_spawn(context, [self, nowNodes = nowNode]() mutable -> boost::asio::awaitable<void> {
                try {

                    if (nowNodes != nullptr) { 

                        co_await boost::asio::async_write(self->socket,
                            boost::asio::buffer(nowNodes->data, nowNodes->length + HEAD_TOTAL_LEN),
                            boost::asio::use_awaitable);

                        NodeQueues::getInstance()->releaseSendNode(nowNodes);

                        nowNodes = nullptr;
                    }

                    // 处理队列中的其他消息
                    SendNode* queuedNode = nullptr;
                    while (self->sendNodes.try_dequeue(queuedNode)) {
                        if (queuedNode) {
                            co_await boost::asio::async_write(self->socket,
                                boost::asio::buffer(queuedNode->data, queuedNode->length + HEAD_TOTAL_LEN),
                                boost::asio::use_awaitable);

                            if (queuedNode) {
                                NodeQueues::getInstance()->releaseSendNode(queuedNode);
                            }
                            queuedNode = nullptr;
                        }
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "CSession::send error: " << e.what() << std::endl;
                    if (!self->isStop) {
                        self->close();
                    }
                }
                }, [self](std::exception_ptr p) {
                    if (p && !self->isStop) {
                        self->close();
                    }
                    });
        }
    }
    catch (std::exception& e) {
        std::cout << "CSession::send (outer try-catch) ERROR:" << e.what() << std::endl;
    }
}


void CSession::handleError(const boost::system::error_code& error, const std::string& context) {
	std::cout << context << " failed! Error: " << error.what() << std::endl;
	close();
}


boost::asio::ip::tcp::socket& CSession::getSocket() {
	return socket;
}

std::string CSession::getSessionId() {

	return sessionID;

}


void CSession::close() {
    // 使用原子操作确保只执行一次
    bool expected = false;
    if (!isStop.compare_exchange_strong(expected, true)) {
        return;  // 已经关闭过了
    }

    // 先停止socket
    boost::system::error_code ec;
    socket.close(ec);

    // 再从服务器移除
    if (server) {
        server->removeSession(this->sessionID);
    }
}





