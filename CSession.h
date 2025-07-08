#pragma once
#include "const.h"
#include "MessageNodes.h"
#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/asio/experimental/channel.hpp>
#include "concurrentqueue.h"
#include "SystemCoroutine.h"

class CServer;

class CSession : public std::enable_shared_from_this<CSession> {
	friend class LogicSystem;
	friend class CServer;
public:
	CSession(boost::asio::io_context& ioContext, CServer* cserver);

	~CSession();

	boost::asio::ip::tcp::socket& getSocket();

	std::string getSessionId();

	void writeAsync(char* msg, int64_t max_length, short msgid);

	void writeAsync(std::string msg, short msgid);

	void close();

private:

	void writerCoroutineAsync(); //使用boost::asio::awaitable和boost::asio::co_spawn的协程 生产者与消费者模式

	boost::asio::ip::tcp::socket socket;

	boost::asio::io_context& context;

	std::string sessionID;

	CServer* server;

	std::atomic<bool> isStop;

	boost::asio::experimental::channel<void(boost::system::error_code)> channel;

	moodycamel::ConcurrentQueue<std::shared_ptr<SendNode>> sendNodes{ 1 };

	void start();

	std::mutex mutexs;

	void handleError(const boost::system::error_code& error, const std::string& context);

};

