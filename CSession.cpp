#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"
#include <boost/uuid/uuid.hpp>            // uuid 类  
#include <boost/uuid/uuid_generators.hpp> // 生成器  
#include <boost/uuid/uuid_io.hpp>   
#include "SessionSendThread.h"
#include "FastMemcpy_Avx.h"
#include <sstream>
#include "Utils.h"


CSession::CSession(boost::asio::io_context& ioContext, CServer* cserver) :socket(ioContext)
, context(ioContext), server(cserver), isStop(false){

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

    systemCoroutine = writerCoroutine();

    boost::asio::co_spawn(context, [self]() -> boost::asio::awaitable<void> {
        // 🔧 关键修复1：使用局部变量而非类成员，避免竞争条件
        char* headerBuffer =(char*) malloc(HEAD_TOTAL_LEN);
        size_t headerSize = sizeof(short) + sizeof(int64_t);

        try {
            while (!self->isStop.load(std::memory_order_acquire)) {
                // 🔧 关键修复2：重置缓冲区
                std::memset(headerBuffer, 0, headerSize);

                // 接收消息头
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
                std::memcpy(&rawMsgId, headerBuffer, sizeof(short));
                std::memcpy(&rawBodyLength, headerBuffer + sizeof(short), sizeof(int64_t));

                short msgId = boost::asio::detail::socket_ops::network_to_host_short(rawMsgId);
                int64_t bodyLength = boost::asio::detail::socket_ops::network_to_host_long(rawBodyLength);

                // 🔧 关键修复4：使用 RAII 智能指针管理内存
                size_t bodySize = static_cast<size_t>(bodyLength);
                char* bodyBuffer = (char*)malloc(bodySize);

                if (!bodyBuffer) {
					LOG_ERROR("Failed to allocate body buffer of size: %zu", bodySize);
                    self->close();
                    co_return;
                }

                // 接收消息体
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

                // 🔧 关键修复5：创建 MessageNode 时立即设置所有字段，避免竞争窗口
                std::shared_ptr<MessageNode> node;
                try {
                    node = std::make_shared<MessageNode>(HEAD_TOTAL_LEN);

                    // 转移所有权到 MessageNode
                    node->data = bodyBuffer;  // 转移所有权
                    node->id = msgId;
                    node->length = bodyLength;
                    node->bufferSize = bodySize;
                    node->session = self;  // 保持 session 引用
                    node->dataSource = MemorySource::NORMAL_NEW;

                }
                catch (const std::exception& e) {
					LOG_ERROR("Failed to create MessageNode: %s", e.what());
                    // bodyBuffer 会自动释放（如果 release() 没被调用）
                    continue;
                }

                LogicSystem::getInstance()->postMessageToQueue(node);
            }
        }
        catch (const std::exception& e) {
			LOG_ERROR("Exception in CSession::start: %s", e.what());
            free(headerBuffer);
			headerBuffer = nullptr;
            self->close();
        }

        }, [self](std::exception_ptr p) {
            if (p) {
                try {
                    std::rethrow_exception(p);
                }
                catch (const boost::system::system_error& e) {
					LOG_ERROR("CSession coroutine error: %s (Code: %s)", e.what(), e.code().value());
                    if (self && !self->isStop.load()) {
                        self->handleError(e.code(), "Coroutine exception");
                    }
                }
                catch (const std::exception& e) {
					LOG_ERROR("CSession coroutine std::exception: %s", e.what());
                    if (self && !self->isStop.load()) {
                        self->handleError(
                            boost::system::errc::make_error_code(boost::system::errc::owner_dead),
                            "std::exception in coroutine"
                        );
                    }
                }
                catch (...) {
					LOG_ERROR("CSession coroutine unknown exception.");
                    if (self && !self->isStop.load()) {
                        self->handleError(
                            boost::system::errc::make_error_code(boost::system::errc::owner_dead),
                            "Unknown exception in coroutine"
                        );
                    }
                }
            }
            });
}

void CSession::send(char* msg, int64_t max_length, short msgid) {
    try {
        // 使用新的安全获取节点方法
        //std::shared_ptr<SendNode> nowNode = NodeQueues::getInstance()->acquireSendNode(msg, max_length, msgid);

        std::shared_ptr<SendNode> nowNode = std::make_shared<SendNode>(msg, max_length, msgid);

        if (nowNode) {
            // 节点已经是干净的，直接设置数据

            if (this->sendNodes.enqueue(nowNode)) {

                if (!systemCoroutine.handle.promise().suspended_) {

                    systemCoroutine.handle.resume();

                }

                nowNode = nullptr;

            }

        }

    }
    catch (std::exception& e) {
		LOG_ERROR("CSession::send ERROR: %s", e.what());
    }
}

void CSession::send(std::string msg, short msgid) {
    try {
        // 使用新的安全获取节点方法
        //std::shared_ptr<SendNode> nowNode = NodeQueues::getInstance()->acquireSendNode(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);

		std::shared_ptr<SendNode> nowNode = std::make_shared<SendNode>(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);

        if (nowNode) {
            // 节点已经是干净的，直接设置数据

            if (this->sendNodes.enqueue(nowNode)) {

                if (!systemCoroutine.handle.promise().suspended_) {

                    systemCoroutine.handle.resume();

                }

                nowNode = nullptr;

            }

        }
    }
    catch (std::exception& e) {
		LOG_ERROR("CSession::send (std::string) ERROR: %s", e.what());
    }
}

// CSession.cpp 中 writerCoroutine() 方法的完整实现

SystemCoroutine CSession::writerCoroutine() {
    auto self = shared_from_this();

    try {
        for (;;) {
            // 等待发送队列中有数据或者会话停止
            while (self->sendNodes.size_approx() == 0 && !self->isStop.load(std::memory_order_acquire)) {
                co_await SystemCoroutine::Awaitable();
            }

            // 如果会话已停止，处理完剩余消息后退出
            if (self->isStop.load(std::memory_order_acquire)) {
                // 处理队列中剩余的消息
                while (self->sendNodes.size_approx() > 0) {
                    std::shared_ptr<SendNode> nowNode = nullptr;
                    if (self->sendNodes.try_dequeue(nowNode) && nowNode != nullptr) {
                        try {
                            boost::asio::write(self->socket, boost::asio::buffer(nowNode->data, nowNode->bufferSize));
                        }
                        catch (const std::exception& e) {
							LOG_ERROR("Error sending message during shutdown: %s", e.what());
                            break; // 发送失败，退出循环
                        }
                    }
                }
                co_return; // 退出协程
            }

            // 处理发送队列中的消息
            std::shared_ptr<SendNode> nowNode = nullptr;
            if (self->sendNodes.try_dequeue(nowNode) && nowNode != nullptr) {
                try {
                    boost::asio::write(self->socket, boost::asio::buffer(nowNode->data, nowNode->bufferSize));
                }
                catch (const boost::system::system_error& e) {
					LOG_ERROR("Socket error in writerCoroutine: %s (Code: %s)", e.what(), e.code().value());
                    self->handleError(e.code(), "Writer coroutine socket error");
                    co_return; // 发生网络错误，退出协程
                }
                catch (const std::exception& e) {
					LOG_ERROR("Exception in writerCoroutine: %s", e.what());
                    co_return; // 发生其他异常，退出协程
                }
            }
        }
    }
    catch (const std::exception& e) {
		LOG_ERROR("Fatal exception in writerCoroutine: %s", e.what());
        if (self && !self->isStop.load()) {
            self->close();
        }
    }

    co_return;
}


void CSession::handleError(const boost::system::error_code& error, const std::string& context) {
	LOG_ERROR("CSession::handleError - %s: %s", context, error.message());
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





