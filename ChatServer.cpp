#include<csignal>
#include"const.h"
#include "CServer.h"
bool isStop = false;
std::condition_variable varible;
std::mutex mutexs;

int main()
{
	ConfigMgr& config = ConfigMgr::Inst();

	std::string host = config["selfServer"]["Host"];

	std::string portStr = config["selfServer"]["Port"];

	std::string url = "setLoginCount";

	try {

		int ports = std::stoi(portStr);

		unsigned short port = static_cast<unsigned short> (ports);

		boost::asio::io_context& ioContext = AsioIOServicePool::getInstance()->GetIOService();

		boost::asio::io_context ioContexts{ 1 };

		boost::asio::signal_set signal(ioContexts, SIGINT, SIGTERM);

		signal.async_wait([&ioContext, &ioContexts](auto,auto) {

			ioContexts.stop();

			ioContext.stop();

			});

		CServer server(ioContexts, port);

		ioContexts.run();
	}
	catch (std::exception& e) {

		std::cout << "The Main Exception is " << e.what() << std::endl;

		return EXIT_FAILURE;

	}
	
	return 0;
}


