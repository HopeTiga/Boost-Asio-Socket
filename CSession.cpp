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
, context(ioContext), server(cserver), isStop(false), writeChannel(ioContext, 1) {

	boost::uuids::random_generator generator;

	boost::uuids::uuid uuids = generator();

	sessionID = boost::uuids::to_string(uuids);

}

CSession::~CSession() {

    close();

}

void CSession::start() {

	writerCoroutineAsync(); // 使用异步版本

    auto self = shared_from_this();

    boost::asio::co_spawn(context, [self]() -> boost::asio::awaitable<void> {

        char headerBuffer[HEAD_TOTAL_LEN];

        size_t headerSize = sizeof(short) + sizeof(int64_t);

        try {
            while (!self->isStop.load()) {

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

                char* bodyBuffer = new char[bodySize];

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

                std::shared_ptr<MessageNode> node;

                try {

                    node = std::make_shared<MessageNode>(HEAD_TOTAL_LEN);

                    node->data = bodyBuffer;  

                    node->id = msgId;

                    node->length = bodyLength;

                    node->bufferSize = bodySize;

                    node->session = self;

                }
                catch (const std::exception& e) {

					LOG_ERROR("Failed to create MessageNode: %s", e.what());
          
                    continue;

                }

                LogicSystem::getInstance()->postMessageToQueue(node);
            }
        }
        catch (const std::exception& e) {

			LOG_ERROR("Exception in CSession::start: %s", e.what());

            self->close();

        }

        }, [this](std::exception_ptr p) {
            if (p) {
                try {

                    std::rethrow_exception(p);

                }
                catch (const boost::system::system_error& e) {
              
                    if (e.code() == boost::asio::error::eof ||
                        e.code() == boost::asio::error::connection_reset) {

                        LOG_INFO("Client disconnected: %s, Session: %s", e.what(), sessionID.c_str());

                    }
                    else {

                        LOG_ERROR("CSession coroutine error: %s (Code: %d), Session: %s",
                            e.what(), e.code().value(), sessionID.c_str());

                    }

                    if (this && !this->isStop.load()) {

                        this->handleError(e.code(), "Coroutine exception");

                    }
                }
                catch (const std::exception& e) {

                    LOG_ERROR("CSession coroutine std::exception: %s, Session: %s", e.what(), sessionID.c_str());

                    if (this && !this->isStop.load()) {

                        this->handleError(
                            boost::system::errc::make_error_code(boost::system::errc::owner_dead),
                            "std::exception in coroutine"
                        );

                    }
                }
                catch (...) {

                    LOG_ERROR("CSession coroutine unknown exception, Session: %s", sessionID.c_str());

                    if (this && !this->isStop.load()) {

                        this->handleError(
                            boost::system::errc::make_error_code(boost::system::errc::owner_dead),
                            "Unknown exception in coroutine"
                        );

                    }
                }
            }
            });
}

void CSession::writeAsync(char* msg, int64_t max_length, short msgid)
{
    try {

        std::shared_ptr<SendNode> nowNode = std::make_shared<SendNode>(msg, max_length, msgid);

        if (nowNode) {

            if (this->sendNodes.enqueue(nowNode)) {

                writeChannel.try_send(boost::system::error_code{});

                nowNode = nullptr;

            }

        }

    }
    catch (std::exception& e) {

        LOG_ERROR("CSession::writeAsync ERROR: %s", e.what());

    }
}

void CSession::writeAsync(std::string msg, short msgid)
{
    try {
 
        std::shared_ptr<SendNode> nowNode = std::make_shared<SendNode>(msg.c_str(), static_cast<int64_t>(msg.size()), msgid);

        if (nowNode) {

            if (this->sendNodes.enqueue(nowNode)) {

                writeChannel.try_send(boost::system::error_code{});

                nowNode = nullptr;

            }

        }
    }
    catch (std::exception& e) {

        LOG_ERROR("CSession::writeAsync (std::string) ERROR: %s", e.what());

    }
}

void CSession::writerCoroutineAsync()
{

    auto self = shared_from_this();

    boost::asio::co_spawn(context, [self]() -> boost::asio::awaitable<void> {
        
        for (;;) {

            std::shared_ptr<SendNode> nowNode = nullptr;

            while (self->sendNodes.try_dequeue(nowNode)) {

                if (nowNode != nullptr) {

                    co_await boost::asio::async_write(self->socket, boost::asio::buffer(nowNode->data, nowNode->bufferSize), boost::asio::use_awaitable);

                }

                nowNode = nullptr;

            }
            
            if(!self->isStop.load()) {

                co_await self->writeChannel.async_receive(boost::asio::use_awaitable);

            }
            else {
                std::shared_ptr<SendNode> nowNode = nullptr;

                while (self->sendNodes.try_dequeue(nowNode)) {

                    if (nowNode != nullptr) {

                        co_await boost::asio::async_write(self->socket, boost::asio::buffer(nowNode->data, nowNode->bufferSize), boost::asio::use_awaitable);

                    }

                    nowNode = nullptr;
                }

                co_return; // 退出协程
            }
        }

        co_return;

		}, [this](std::exception_ptr p) {

            if (p) {

                try {

                    std::rethrow_exception(p);

                }
                catch (const boost::system::system_error& e) {
 
                    if (e.code() == boost::asio::error::eof ||
                        e.code() == boost::asio::error::connection_reset) {

                        LOG_INFO("Client disconnected: %s, Session: %s", e.what(), sessionID.c_str());

                    }
                    else {

                        LOG_ERROR("CSession coroutine error: %s (Code: %d), Session: %s",
                            e.what(), e.code().value(), sessionID.c_str());

                    }

                    if (this && !this->isStop.load()) {

                        this->handleError(e.code(), "Coroutine exception");

                    }
                }
                catch (const std::exception& e) {

                    LOG_ERROR("CSession coroutine std::exception: %s, Session: %s", e.what(), sessionID.c_str());

                    if (this && !this->isStop.load()) {
                        this->handleError(
                            boost::system::errc::make_error_code(boost::system::errc::owner_dead),
                            "std::exception in coroutine"
                        );
                    }

                }
                catch (...) {

                    LOG_ERROR("CSession coroutine unknown exception, Session: %s", sessionID.c_str());

                    if (this && !this->isStop.load()) {

                        this->handleError(
                            boost::system::errc::make_error_code(boost::system::errc::owner_dead),
                            "Unknown exception in coroutine"
                        );

                    }
                }
            }
            });
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

    bool expected = false;

    if (!isStop.compare_exchange_strong(expected, true)) {

        return; 
    
    }

    boost::system::error_code ec;

    socket.close(ec);

    if (server) {

        server->removeSession(this->sessionID);

    }
}





