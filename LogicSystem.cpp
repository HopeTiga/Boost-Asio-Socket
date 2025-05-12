#include "LogicSystem.h"
#include "UserMgr.h"

LogicSystem::LogicSystem(size_t size ) :isStop(false), threadSize(size){

	registerCallBackFunction();
}

void LogicSystem::initializeThreads() {
	
	auto self = shared_from_this();
	for (int i = 0; i < threadSize; i++) {
		threads.emplace_back(std::thread([self]() {
			self->processMessage(self);
			}));
	}
}

LogicSystem::~LogicSystem() {

	isStop = true;

	condition.notify_all();

	for (auto& thread : threads) {
		if (thread.joinable()) {
			thread.join();
		}
	}

}

void LogicSystem::processMessage(std::shared_ptr<LogicSystem> logicSystem) {

	for (;;) {

		{
			std::unique_lock<std::mutex> lock(mutexs);

			while (logicSystem->messageNodes.empty() && !isStop) {
				condition.wait(lock);
			}
		}

		if (isStop) {

			while (!logicSystem->messageNodes.empty()) {

				MessageNode * nowNode;

				logicSystem->messageNodes.pop(nowNode);

				if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {

					std::cout << "The MessageID" << nowNode->id << "is no corresponding CallBackFunctions" << std::endl;

					continue;
				}

				callBackFunctions[nowNode->id](nowNode->session, nowNode->id, std::string(nowNode->data, nowNode->length));

				if (nowNode != nullptr) {

					NodeQueues::getInstantce()->releaseMessageNode(nowNode);

				}
			}

		}

		MessageNode * nowNode;

		logicSystem->messageNodes.pop(nowNode);

		if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {

			std::cout << "The MessageID is no corresponding CallBackFunctions" << std::endl;

			continue;
		}

		callBackFunctions[nowNode->id](nowNode->session, nowNode->id, std::string(nowNode->data, nowNode->length));

		if (nowNode != nullptr) {

			NodeQueues::getInstantce()->releaseMessageNode(nowNode);

		}

	}

}

void LogicSystem::postMessageToQueue(MessageNode* node) {

	messageNodes.push(node);

	condition.notify_one();

}

void LogicSystem::registerCallBackFunction() {

	callBackFunctions[1001] = std::bind(&LogicSystem::boostAsioTcpSocket, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

}

std::vector<std::string> getServers() {

	std::vector<std::string> serverList;

	std::string serverStringList = ConfigMgr::Inst()["chatservers"]["Name"];

	std::stringstream stream(serverStringList);

	std::string server;

	while (std::getline(stream, server, ',')) {

		serverList.push_back(server);

	}

	return serverList;

}

void LogicSystem::boostAsioTcpSocket(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data) {

	std::cout << "boostAsioTcpSocket::Coroutine : " << msg_data << std::endl;

	session->send("boostAsioTcpSocket::Coroutine CPlusPlus20", 1001);

}

