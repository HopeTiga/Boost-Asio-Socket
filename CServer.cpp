#include "CServer.h"
#include "LogicSystem.h"


//tcp::v4()��ʾ���յ�ip��Χ,port�����ַ;
CServer::CServer(boost::asio::io_context& ioContext, unsigned short& port)
	:c_ioContext(ioContext),c_accept(ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::any(), port)){

	LogicSystem::getInstance()->initializeThreads();

	startAccept();
}


void CServer::startAccept() {

	std::cout << "CServer::startAccept()" << std::endl;

	boost::asio::co_spawn(c_ioContext, [this]() ->boost::asio::awaitable<void> {

		for (;;) {
			boost::asio::io_context& ioContext = AsioIOServicePool::getInstance()->GetIOService();

			std::shared_ptr<CSession> session = std::make_shared<CSession>(ioContext, this);

			co_await c_accept.async_accept(session->getSocket(), boost::asio::use_awaitable);

			session->getSocket().set_option(boost::asio::ip::tcp::no_delay(true));

			std::cout << "Session Async_accpet IP: " << session->getSocket().remote_endpoint().address().to_v4().to_string() << ":" << session->getSocket().remote_endpoint().port() << std::endl;

			{
				std::lock_guard<std::mutex> guard(mutexs);

				sessionMap.insert(std::pair<std::string, std::shared_ptr<CSession>>(session->getSessionId(), session));
			}

			session->start();
			
		}

		}, [](std::exception_ptr p) {

			if (p) {
				try {
					std::rethrow_exception(p);
				}
				catch (const boost::system::system_error& e) {
					std::cerr << "Server client_handler coroutine (Boost.System error): "
						<< e.what() << " (Code: " << e.code() << " - " << e.code().message() << ")" << std::endl;
				}
				catch (const std::exception& e) {
					std::cerr << "Server client_handler coroutine (std::exception): " << e.what() << std::endl;
				}
				catch (...) {
					std::cerr << "Server client_handler coroutine (unknown exception)." << std::endl;
				}
			}

			});

}
