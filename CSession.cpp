#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"
#include <boost/uuid/uuid.hpp>            // uuid 类  
#include <boost/uuid/uuid_generators.hpp> // 生成器  
#include <boost/uuid/uuid_io.hpp>   
#include "SessionSendThread.h"

CSession::CSession(boost::asio::io_context& ioContext, CServer* cserver) :socket(ioContext)
, context(ioContext), server(cserver), isStop(false),buffers(4096),node(nullptr) {

	boost::uuids::random_generator generator;

	boost::uuids::uuid uuids = generator();

	sessionID = boost::uuids::to_string(uuids);

	//node = new MessageNode(HEAD_TOTAL_LEN);
}

CSession::~CSession() {
	if (node != nullptr) {
		delete node;
		node = nullptr;
	}
}

void CSession::start() {

    auto self = shared_from_this();
    boost::asio::co_spawn(context, [self]() -> boost::asio::awaitable<void> {
        char* headerBuffer = nullptr;
        char* bodyBuffer = nullptr;
        int64_t bodyLength = 0;
        short msgId = 0;

        try {
            while (!self->isStop) {
                // 接收消息头
                headerBuffer = BufferPool::getInstance()->getBuffer(sizeof(short) + sizeof(int64_t));

                if (headerBuffer == nullptr) headerBuffer = new char[sizeof(short) + sizeof(int64_t)];

                size_t headerRead = 0;
                
                while (headerRead < sizeof(short) + sizeof(int64_t)) {
                    size_t n = co_await self->socket.async_read_some(
                        boost::asio::buffer(headerBuffer + headerRead, sizeof(short) + sizeof(int64_t) - headerRead),
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
                
                // 使用memcpy代替指针强制转换
                std::memcpy(&rawMsgId, headerBuffer, sizeof(short));
                std::memcpy(&rawBodyLength, headerBuffer + sizeof(short), sizeof(int64_t));

                msgId = boost::asio::detail::socket_ops::network_to_host_short(rawMsgId);
                bodyLength = boost::asio::detail::socket_ops::network_to_host_long(rawBodyLength);
                
                BufferPool::getInstance()->releaseBuffer(headerBuffer);
                headerBuffer = nullptr;

                // 接收消息体
                bodyBuffer = BufferPool::getInstance()->getBuffer(bodyLength);

                if (bodyBuffer == nullptr) bodyBuffer = new char[bodyLength];

                size_t bodyRead = 0;
                
                while (bodyRead < bodyLength) {
                    size_t n = co_await self->socket.async_read_some(
                        boost::asio::buffer(bodyBuffer + bodyRead, bodyLength - bodyRead),
                        boost::asio::use_awaitable);
                    
                    if (n == 0) {
                        self->close();
                        co_return;
                    }
                    bodyRead += n;
                }

                // 提交到处理队列
                MessageNode* node = nullptr;
                if (!NodeQueues::getInstantce()->getMessageNode(node)) {
                    node = new MessageNode(sizeof(short) + sizeof(int64_t));
                }

                if (node == nullptr) {
                    node = new MessageNode(sizeof(short) + sizeof(int64_t));
                }
                
                node->id = msgId;
                node->length = bodyLength;
                node->data = bodyBuffer;
                node->fromPool = true;
                node->session = self;
                
                LogicSystem::getInstance()->postMessageToQueue(node);
                bodyBuffer = nullptr;
            }
        }
        catch (...) {
            if (headerBuffer) BufferPool::getInstance()->releaseBuffer(headerBuffer);
            if (bodyBuffer) BufferPool::getInstance()->releaseBuffer(bodyBuffer);
            self->close();
        }
    }, [self](std::exception_ptr p) { 
        if (p) {
            try {
                std::rethrow_exception(p);
            }
            catch (const boost::system::system_error& e) {
                // 这个处理器主要捕获协程内部未被try-catch块捕获的Boost.System异常
                // 如果之前的try-catch已经调用了handleError并co_return，这里可能不会执行
                // 或者在handleError之后，self可能已失效，需谨慎使用self
                std::cerr << "CSession unhandled coroutine (Boost.System error): "
                    << e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
                // 确保会话被清理，如果尚未清理
                if (self && !self->isStop) { // 检查self是否有效以及会话是否已停止
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

        SendNode* nowNode = nullptr;

        if (NodeQueues::getInstantce()->getSendNode(nowNode)) {
            if (nowNode != nullptr) {
                nowNode->setSendNode(msg, max_length, msgid);
            }
        }

        if (nowNode == nullptr) {

            nowNode = new SendNode(msg, max_length, msgid);

        }

        auto self = shared_from_this();

		if (nowNode) { // 确保 nowNode 有效
            // 修改 lambda 捕获方式
			boost::asio::co_spawn(context, [self, nodeToSend = nowNode]() ->boost::asio::awaitable<void> {
                try {
                    co_await boost::asio::async_write(self->socket, boost::asio::buffer(nodeToSend->data, nodeToSend->length + HEAD_TOTAL_LEN),
                        boost::asio::use_awaitable);

                    if (nodeToSend != nullptr) {
                        NodeQueues::getInstantce()->releaseSendNode(nodeToSend);
                    }

                    SendNode* queuedNode = nullptr;
                    // 处理 sendNodes 队列中可能存在的其他消息
                    while (self->sendNodes.pop(queuedNode)) {
                        // 使用新的变量名
                        if (queuedNode) {
                            co_await boost::asio::async_write(self->socket, boost::asio::buffer(queuedNode->data, queuedNode->length + HEAD_TOTAL_LEN),
                                boost::asio::use_awaitable);
                            NodeQueues::getInstantce()->releaseSendNode(queuedNode);
                            queuedNode = nullptr;
                        }
                    }
                } catch (const boost::system::system_error& e) {
                    // 如果 handleError 内部会关闭 session 并设置 isStop，则这里可能不需要再次调用
                    // 但记录特定于发送的错误可能仍然有用
                    std::cerr << "CSession::send coroutine (Boost.System error): "
                              << e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
                    if (!self->isStop) { // 避免在已处理的会话上重复操作
                        self->handleError(e.code(), "CSession::send async_write");
                    }
                    // co_return; // 发生错误，退出此发送协程
                } catch (const std::exception& e) {
                    std::cerr << "CSession::send coroutine (std::exception): " << e.what() << std::endl;
                    if (!self->isStop) {
                         self->handleError(boost::system::errc::make_error_code(boost::system::errc::io_error), "CSession::send exception");
                    }
                    // co_return;
                }
				}, [self](std::exception_ptr p) { // 协程的顶层异常处理器
					if (p) {
						try {
							std::rethrow_exception(p);
						}
						catch (const boost::system::system_error& e) {
							std::cerr << "CSession::send top-level coroutine handler (Boost.System error): "
								<< e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
                            // 确保会话被清理，如果尚未清理
                            if (self && !self->isStop) {
                                 self->handleError(e.code(), "CSession::send unhandled Boost.System exception");
                            }
						}
						catch (const std::exception& e) {
							std::cerr << "CSession::send top-level coroutine handler (std::exception): " << e.what() << std::endl;
                            if (self && !self->isStop) {
                                self->handleError(boost::system::errc::make_error_code(boost::system::errc::owner_dead), "CSession::send unhandled std::exception");
                            }
						}
						catch (...) {
							std::cerr << "CSession::send top-level coroutine handler (unknown exception)." << std::endl;
                            if (self && !self->isStop) {
                                self->handleError(boost::system::errc::make_error_code(boost::system::errc::owner_dead), "CSession::send unhandled unknown exception");
                            }
						}
					}
				});
		} else {
            // nowNode 为 nullptr 的情况，可能创建失败或从池获取失败
            std::cerr << "CSession::send: nowNode is nullptr before co_spawn." << std::endl;
        }
	}
	catch (std::exception& e) {
		std::cout << "CSession::send (outer try-catch) ERROR:" << e.what() << std::endl;
        // 考虑是否需要更全面的错误处理，例如关闭会话
	}
}

void CSession::send(std::string msg, short msgid) {
	try {
        SendNode* nowNode = nullptr;
        if (NodeQueues::getInstantce()->getSendNode(nowNode)) {
            if (nowNode != nullptr) {
                nowNode->setSendNode(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);
            }
        }
        if (nowNode == nullptr) {

            nowNode = new SendNode(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);

        }

        auto self = shared_from_this();

		if (nowNode) { // 确保 nowNode 有效
            // 修改 lambda 捕获方式
			boost::asio::co_spawn(context, [self, nodeToSend = nowNode]() ->boost::asio::awaitable<void> {
                try {
                    co_await boost::asio::async_write(self->socket, boost::asio::buffer(nodeToSend->data, nodeToSend->length + HEAD_TOTAL_LEN),
                        boost::asio::use_awaitable);

                    if (nodeToSend != nullptr) {
                        NodeQueues::getInstantce()->releaseSendNode(nodeToSend);
                    }
                    SendNode* queuedNode = nullptr;
                    // 处理 sendNodes 队列中可能存在的其他消息
                    while (self->sendNodes.pop(queuedNode)) {
                        // 使用新的变量名
                        if (queuedNode) {
                            co_await boost::asio::async_write(self->socket, boost::asio::buffer(queuedNode->data, queuedNode->length + HEAD_TOTAL_LEN),
                                boost::asio::use_awaitable);
                            NodeQueues::getInstantce()->releaseSendNode(queuedNode);
                            queuedNode = nullptr;
                        }
                    }
                } catch (const boost::system::system_error& e) {
                    std::cerr << "CSession::send coroutine (Boost.System error): "
                              << e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
                    if (!self->isStop) {
                        self->handleError(e.code(), "CSession::send async_write");
                    }
                } catch (const std::exception& e) {
                    std::cerr << "CSession::send coroutine (std::exception): " << e.what() << std::endl;
                    if (!self->isStop) {
                        self->handleError(boost::system::errc::make_error_code(boost::system::errc::io_error), "CSession::send exception");
                    }
                }
				}, [self](std::exception_ptr p) { // 协程的顶层异常处理器
					if (p) {
						try {
							std::rethrow_exception(p);
						}
						catch (const boost::system::system_error& e) {
							std::cerr << "CSession::send top-level coroutine handler (Boost.System error): "
								<< e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
                            if (self && !self->isStop) {
                                 self->handleError(e.code(), "CSession::send unhandled Boost.System exception");
                            }
						}
						catch (const std::exception& e) {
							std::cerr << "CSession::send top-level coroutine handler (std::exception): " << e.what() << std::endl;
                            if (self && !self->isStop) {
                                self->handleError(boost::system::errc::make_error_code(boost::system::errc::owner_dead), "CSession::send unhandled std::exception");
                            }
						}
						catch (...) {
							std::cerr << "CSession::send top-level coroutine handler (unknown exception)." << std::endl;
                            if (self && !self->isStop) {
                                self->handleError(boost::system::errc::make_error_code(boost::system::errc::owner_dead), "CSession::send unhandled unknown exception");
                            }
						}
					}
				});
		} else {
             // nowNode 为 nullptr 的情况
            std::cerr << "CSession::send: nowNode is nullptr before co_spawn." << std::endl;
        }
	}
	catch (std::exception& e) {
		std::cout << "CSession::send (outer try-catch) ERROR:" << e.what() << std::endl;
	}
}

void CSession::handleError(const boost::system::error_code& error, const std::string& context) {
	std::cout << context << " failed! Error: " << error.what() << std::endl;
	close();
	server->ClearSession(sessionID);
}


boost::asio::ip::tcp::socket& CSession::getSocket() {
	return socket;
}

std::string CSession::getSessionId() {

	return sessionID;

}

int CSession::getUserId() {

	return userId;

}

void CSession::setUserId(int uid) {

	userId = uid;

}


void CSession::close() {

	socket.close();

	isStop = true;

}





