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

	CServer(boost::asio::io_context &ioContext,unsigned short& port,size_t size = 1024);

	void removeSession(std::string sessionId);

private:

	void startAccept();

	//���նԶ˵�����;
	boost::asio::ip::tcp::acceptor c_accept;
	//������
	boost::asio::io_context& c_ioContext;
	//socket���նԶ���Ϣ;

	std::vector<std::map<std::string, std::shared_ptr<CSession>>> sessions;

	std::vector<std::mutex> sessionMutexs;

	size_t hashSize;

	std::atomic<size_t> connections;
};