#include "CServer.h"
#include "LogicSystem.h"
#include "Utils.h"


//tcp::v4()表示接收的ip范围,port代表地址;
CServer::CServer(boost::asio::io_context& ioContext, unsigned short& port,size_t size)
	:c_ioContext(ioContext),c_accept(ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::any(), port))
,sessions(size), sessionMutexs(size), hashSize(size){

	LogicSystem::getInstance()->initializeThreads();

	c_accept.set_option(boost::asio::ip::tcp::no_delay(true));

	startAccept();
}

void CServer::removeSession(std::string sessionId)
{
	LOG_WARNING("CServer::removeSession() sessionId: %s", sessionId.c_str());

	size_t hashValue = std::hash<std::string>{}(sessionId);

	hashValue = hashValue % this->hashSize;

	{
		std::lock_guard<std::mutex> guard(this->sessionMutexs[hashValue]);

		sessions[hashValue].erase(sessionId);

		connections--;
	}

}


void CServer::startAccept() {

	LOG_INFO("CServer::startAccept");

	boost::asio::co_spawn(c_ioContext, [this]() ->boost::asio::awaitable<void> {

		for (;;) {
			boost::asio::io_context& ioContext = AsioProactors::getInstance()->getIoComplatePorts();

			std::shared_ptr<CSession> session = std::make_shared<CSession>(ioContext, this);

			co_await c_accept.async_accept(session->getSocket(), boost::asio::use_awaitable);

			//std::cout << "Session Async_accpet IP: " << session->getSocket().remote_endpoint().address().to_v4().to_string() << ":" << session->getSocket().remote_endpoint().port() << std::endl;

			size_t hashValue = std::hash<std::string>{}(session->getSessionId());

			hashValue = hashValue % this->hashSize;

			{
				std::lock_guard<std::mutex> guard(this->sessionMutexs[hashValue]);

				sessions[hashValue].insert(std::pair<std::string, std::shared_ptr<CSession>>(session->getSessionId(), session));
			}

			connections++;

			session->start();
			
		}

		}, [](std::exception_ptr p) {

			if (p) {
				try {
					std::rethrow_exception(p);
				}
				catch (const boost::system::system_error& e) {
					LOG_ERROR("Server client_handler coroutine (Boost.System error): %s", e.what());

				}
				catch (const std::exception& e) {
					LOG_ERROR("Server client_handler coroutine (std::exception): %s", e.what());
				}
				catch (...) {
					LOG_ERROR("Server client_handler coroutine (unknown exception).");
				}
			}

			});

}
