#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <mutex>
#include <iostream>
#include <string>
#include <map>
#include "CSession.h"

class CServer{
public:

	CServer(boost::asio::io_context &ioContext,unsigned short& port);

private:

	void startAccept();

	//接收对端的链接;
	boost::asio::ip::tcp::acceptor c_accept;
	//上下文
	boost::asio::io_context& c_ioContext;
	//socket接收对端信息;

	std::map<std::string, std::shared_ptr<CSession>> sessionMap;

	std::mutex mutexs;
};
