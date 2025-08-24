#pragma once
#include "const.h"
#include "MessageNodes.h"
#include "CSession.h"
#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include "concurrentqueue.h"
#include "Singleton.h"


class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
	friend class Singleton<LogicSystem>;

public:


	~LogicSystem();

	LogicSystem(const LogicSystem& logic) = delete;

	void operator=(const LogicSystem& logic) = delete;

	void postMessageToQueue(std::shared_ptr<MessageNode> node);

	void initializeThreads();

private:

	void registerCallBackFunction();

	LogicSystem(size_t minSize = std::thread::hardware_concurrency() * 2, size_t maxSize = std::thread::hardware_concurrency() * 4);

	void processMessageTemporary(std::shared_ptr<LogicSystem> logicSystem);

	moodycamel::ConcurrentQueue<std::shared_ptr<MessageNode>> messageNodes;

	std::map<short, std::function<void(std::shared_ptr<CSession>,
		const short& msg_id, const std::string& msg_data)>> callBackFunctions;

	std::vector<std::thread> threads;

	std::atomic<bool> isStop;

	void boostAsioTcpSocket(std::shared_ptr<CSession>,
		const short& msg_id, const std::string& msg_data);

	size_t minSize;

	size_t maxSize;

	std::atomic<size_t> nowSize;

	std::thread metricsThread;

	std::chrono::milliseconds updateInterval{ 10000 };

	std::atomic<int> pressuresCount{ 0 };

	std::vector<boost::asio::io_context> ioContexts;

	std::vector<std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>> works;

	boost::lockfree::queue<int> readyQueue;

	std::vector<std::unique_ptr<boost::asio::experimental::concurrent_channel<void(boost::system::error_code)>>> channels;

};


