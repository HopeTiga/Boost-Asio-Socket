#pragma once
#include "const.h"
#include "MessageNodes.h"
#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>

extern class CServer;

class CSession : public std::enable_shared_from_this<CSession> {
	friend class LogicSystem;
	friend class CServer;
public:
	CSession(boost::asio::io_context& ioContext, CServer* cserver);

	~CSession();

	boost::asio::ip::tcp::socket& getSocket();

	std::string getSessionId();

	void send(char* msg, int64_t max_length, short msgid);

	void send(std::string msg, short msgid);

private:

	boost::asio::ip::tcp::socket socket;

	boost::asio::io_context& context;

	std::string sessionID;

	CServer* server;

	std::atomic<bool> isStop;

	void close();

	boost::lockfree::queue<SendNode*> sendNodes;

	void start();

	std::mutex mutexs;

	void handleError(const boost::system::error_code& error, const std::string& context);

};

