#include<csignal>
#include"const.h"
#include "CServer.h"


int main()
{
	ConfigMgr& config = ConfigMgr::Inst();

	std::string host = config["SelfServer"]["Host"];

	std::string portStr = config["SelfServer"]["Port"];

	try {

		int ports = std::stoi(portStr);

		unsigned short port = static_cast<unsigned short> (ports);

		boost::asio::io_context& ioContext = AsioProactors::getInstance()->getIoComplatePorts();

		boost::asio::io_context ioContexts{ 1 };

		boost::asio::signal_set signal(ioContexts, SIGINT, SIGTERM);

		signal.async_wait([&ioContext, &ioContexts](auto,auto) {

			ioContexts.stop();

			ioContext.stop();

			});


		// 添加启动LOGO
		std::cout << R"(
             _____  _____  ____    _____  ____   _____    ____   _    _  _______  _____  _   _  ______ 
     /\     / ____||_   _|/ __ \  / ____|/ __ \ |  __ \  / __ \ | |  | ||__   __||_   _|| \ | ||  ____|
    /  \   | (___    | | | |  | || |    | |  | || |__) || |  | || |  | |   | |     | |  |  \| || |__   
   / /\ \   \___ \   | | | |  | || |    | |  | ||  _  / | |  | || |  | |   | |     | |  | . ` ||  __|  
  / ____ \  ____) | _| |_| |__| || |____| |__| || | \ \ | |__| || |__| |   | |    _| |_ | |\  || |____ 
 /_/    \_\|_____/ |_____|\____/  \_____|\____/ |_|  \_\ \____/  \____/    |_|   |_____||_| \_||______|                                                                                                                                                                                               
    )" << std::endl;
		// ... existing code ...

		std::cout << "The AsioCoroutine is start in " << port << std::endl;

		CServer server(ioContexts, port);

		ioContexts.run();
	}
	catch (std::exception& e) {

		std::cout << "The Main Exception is " << e.what() << std::endl;

		return EXIT_FAILURE;

	}
	
	return 0;
}


