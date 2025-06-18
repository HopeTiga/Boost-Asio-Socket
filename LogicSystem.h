#pragma once
#include "const.h"
#include "MessageNodes.h"
#include "CSession.h"
#include <boost/lockfree/queue.hpp>
#include "concurrentqueue.h"
#include "SystemCoroutine.h"
#include "Singleton.h"

struct MessagePressureMetrics {
	std::atomic<size_t> pendingMessages{ 0 };        // 待处理消息计数
	std::atomic<size_t> totalProcessed{ 0 };         // 总处理消息数
	std::atomic<size_t> busyCoroutines{ 16 };         // 忙碌的协程数
	std::atomic<double> avgProcessingTime{ 0.0 };    // 平均处理时间(毫秒)
	std::atomic<size_t> totalProcessingTime{ 0 };    // 峰值待处理消息数

	// 计算压力指标
	double getMessagePressure(size_t totalCoroutines) const {
		if (totalCoroutines == 0) return 0.0;

		// 基于多个维度计算压力
		double pendingPressure = static_cast<double>(pendingMessages.load()) / (totalCoroutines * 3000); // 假设每个协程能缓冲5个消息
		double busyRatio = static_cast<double>(busyCoroutines.load()) / totalCoroutines;
		double timePressure = std::min(1.0, avgProcessingTime.load() / 10.0); // 10ms为基准

		// 综合压力指标
		return std::min(1.0, (pendingPressure * 0.5 + busyRatio * 0.3 + timePressure * 0.2));
	}
};

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

	SystemCoroutine processMessage(std::shared_ptr<LogicSystem> logicSystem);

	void processMessageTemporary(std::shared_ptr<LogicSystem> logicSystem);

	std::mutex mutexs;

	moodycamel::ConcurrentQueue<std::shared_ptr<MessageNode>> messageNodes;

	std::map<short, std::function<void(std::shared_ptr<CSession>,
		const short& msg_id, const std::string& msg_data)>> callBackFunctions;

	std::condition_variable condition;

	std::vector<std::thread> threads;

	bool isStop;

	void boostAsioTcpSocket(std::shared_ptr<CSession>,
		const short& msg_id, const std::string& msg_data);

	SystemCoroutine* systemCoroutines;

	size_t minSize;

	size_t maxSize;

	std::atomic<size_t> nowSize;

	std::thread metricsThread;

	std::chrono::milliseconds updateInterval{ 10000 };

	boost::lockfree::queue<int> readyQueue;

};


