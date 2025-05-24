#pragma once
#include "const.h"
#include "MessageNodes.h"
#include "CSession.h"
#include <queue>
#include <boost/lockfree/queue.hpp>
#include "SystemCoroutline.h"

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
	friend class Singleton<LogicSystem>;

public:


	~LogicSystem();

	LogicSystem(const LogicSystem& logic) = delete;

	void operator=(const LogicSystem& logic) = delete;

	void postMessageToQueue(MessageNode* node);

	void initializeThreads();

private:

	void registerCallBackFunction();

	LogicSystem(size_t size = std::thread::hardware_concurrency() * 2);

	SystemCoroutline processMessage(std::shared_ptr<LogicSystem> logicSystem);

	std::mutex mutexs;

	boost::lockfree::queue<MessageNode*> messageNodes;

	std::map<short, std::function<void(std::shared_ptr<CSession>,
		const short& msg_id, const std::string& msg_data)>> callBackFunctions;

	std::condition_variable condition;

	std::vector<std::thread> threads;

	size_t threadSize;

	bool isStop;

	void boostAsioTcpSocket(std::shared_ptr<CSession>,
		const short& msg_id, const std::string& msg_data);
	 
	SystemCoroutline * systemCoroutlines;

};


