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

	//���նԶ˵�����;
	boost::asio::ip::tcp::acceptor c_accept;
	//������
	boost::asio::io_context& c_ioContext;
	//socket���նԶ���Ϣ;

	std::map<std::string, std::shared_ptr<CSession>> sessionMap;

	std::mutex mutexs;
};