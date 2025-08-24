#pragma once
#include "const.h"
#include "MessageNodes.h"
#include <boost/asio.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include "concurrentqueue.h"

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

	void start();

	void close();

private:

	void writerCoroutineAsync(); //ʹ��boost::asio::awaitable��boost::asio::co_spawn��Э�� ��������������ģʽ

	void handleError(const boost::system::error_code& error, const std::string& context);

private:

	boost::asio::ip::tcp::socket socket;

	boost::asio::io_context& context;

	std::string sessionID;

	CServer* server;

	std::atomic<bool> isStop;

	moodycamel::ConcurrentQueue<std::shared_ptr<SendNode>> sendNodes{ 1 };

	std::mutex mutexs;

	boost::asio::experimental::concurrent_channel<void(boost::system::error_code)> writeChannel;


};

