#pragma once
#include "const.h"
#include "MessageNodes.h"
#include "CSession.h"
#include <boost/lockfree/queue.hpp>
#include "concurrentqueue.h"
#include "SystemCoroutine.h"
#include "Singleton.h"

struct MessagePressureMetrics {
	std::atomic<size_t> pendingMessages{ 0 };        // ��������Ϣ����
	std::atomic<size_t> totalProcessed{ 0 };         // �ܴ�����Ϣ��
	std::atomic<size_t> busyCoroutines{ 16 };         // æµ��Э����
	std::atomic<double> avgProcessingTime{ 0.0 };    // ƽ������ʱ��(����)
	std::atomic<size_t> totalProcessingTime{ 0 };    // ��ֵ��������Ϣ��

	// ����ѹ��ָ��
	double getMessagePressure(size_t totalCoroutines) const {
		if (totalCoroutines == 0) return 0.0;

		// ���ڶ��ά�ȼ���ѹ��
		double pendingPressure = static_cast<double>(pendingMessages.load()) / (totalCoroutines * 3000); // ����ÿ��Э���ܻ���5����Ϣ
		double busyRatio = static_cast<double>(busyCoroutines.load()) / totalCoroutines;
		double timePressure = std::min(1.0, avgProcessingTime.load() / 10.0); // 10msΪ��׼

		// �ۺ�ѹ��ָ��
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


