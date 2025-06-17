#include "LogicSystem.h"
#include <chrono>
#include "Utils.h"

MessagePressureMetrics metrics;

LogicSystem::LogicSystem(size_t minSize, size_t maxSize) :minSize(minSize), maxSize(maxSize), nowSize(minSize),isStop(false), threads(maxSize)
,systemCoroutines(new SystemCoroutine[maxSize]) {

	registerCallBackFunction();

}

void LogicSystem::initializeThreads() {
	
	for (int i = 0; i < nowSize; i++) {
		threads.emplace_back(std::thread([this,i]() {
			systemCoroutines[i] = processMessage(shared_from_this());
            systemCoroutines[i].handle.promise().setTargetQueue(&readyQueue);
            systemCoroutines[i].setQueue(&readyQueue);
			systemCoroutines[i].handle.promise().storeIndex(i);
  
			}));
	}

	metricsThread = std::move(std::thread([this]() {
		
		while (!isStop) {

            double pressures = metrics.getMessagePressure(this->nowSize);

            LOG_INFO("LogicSystem: Monitoring system Threads: %u", nowSize.load());

			LOG_INFO("LogicSystem: Message Pressure: %0.2f", pressures);

			if (pressures > 0.7) {

				std::lock_guard<std::mutex> lock(mutexs);

				if (this->nowSize == this->maxSize) {

					std::this_thread::sleep_for(updateInterval);

					continue;
				}

				size_t newIndex = this->nowSize.load();    // 先获取当前大小作为新索引

				this->nowSize.fetch_add(1);

				threads[newIndex] = std::move(std::thread([this, newIndex]() {
					processMessageTemporary(shared_from_this());
					}));

                threads[newIndex].detach();
			}

			std::this_thread::sleep_for(updateInterval);
		}

		}));


}

LogicSystem::~LogicSystem() {

	isStop = true;

	condition.notify_all();

	for (auto& thread : threads) {
		if (thread.joinable()) {
			thread.join();
		}
	}

	if (systemCoroutines != nullptr) {
		delete[] systemCoroutines;
		systemCoroutines = nullptr;
	}

}

// LogicSystem.cpp 中的关键修改部分

SystemCoroutine LogicSystem::processMessage(std::shared_ptr<LogicSystem> logicSystem) {
    for (;;) {
        while (logicSystem->messageNodes.size_approx() == 0 && !isStop) {
            metrics.busyCoroutines--;
            co_await SystemCoroutine::Awaitable();
        }

        if (isStop) {
            while (!logicSystem->messageNodes.size_approx() == 0) {
                std::shared_ptr<MessageNode> nowNode = nullptr;
                logicSystem->messageNodes.try_dequeue(nowNode);

                if (nowNode != nullptr) {
                    // 🔧 处理消息后释放引用
                    if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {

						LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);

                        }
                    else {
                        // 处理消息
                        long long start = std::chrono::floor<std::chrono::milliseconds>(
                            std::chrono::system_clock::now()
                        ).time_since_epoch().count();

                        std::string msgData(nowNode->data, nowNode->length);
                        callBackFunctions[nowNode->id](nowNode->session, nowNode->id, msgData);

                        long long end = std::chrono::floor<std::chrono::milliseconds>(
                            std::chrono::system_clock::now()
                        ).time_since_epoch().count();

                        metrics.totalProcessingTime += (end - start);
                        metrics.totalProcessed++;

                    }

                    metrics.pendingMessages--;

                }
            }
            co_return;
        }

        std::shared_ptr<MessageNode> nowNode = nullptr;
        logicSystem->messageNodes.try_dequeue(nowNode);

        if (nowNode != nullptr) {
            if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {

                LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);

            }
            else {
                // 处理消息
                long long start = std::chrono::floor<std::chrono::milliseconds>(
                    std::chrono::system_clock::now()
                ).time_since_epoch().count();

                std::string msgData(nowNode->data, nowNode->length);
                callBackFunctions[nowNode->id](nowNode->session, nowNode->id, msgData);


                long long end = std::chrono::floor<std::chrono::milliseconds>(
                    std::chrono::system_clock::now()
                ).time_since_epoch().count();

                metrics.totalProcessingTime += (end - start);
                metrics.totalProcessed++;
            }

            metrics.pendingMessages--;

        }
    }

    co_return;
}

void LogicSystem::processMessageTemporary(std::shared_ptr<LogicSystem> logicSystem) {
    for (;;) {
        while (logicSystem->messageNodes.size_approx() == 0 && !isStop) {
            metrics.busyCoroutines--;
            this->nowSize.fetch_sub(1);
            return;
        }

        if (isStop) {
            while (!logicSystem->messageNodes.size_approx() == 0) {
                std::shared_ptr<MessageNode> nowNode = nullptr;
                logicSystem->messageNodes.try_dequeue(nowNode);

                if (nowNode != nullptr) {
                    if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {
                        LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);
                    }
                    else {
                        // 处理消息
                        long long start = std::chrono::floor<std::chrono::milliseconds>(
                            std::chrono::system_clock::now()
                        ).time_since_epoch().count();

                        std::string msgData(nowNode->data, nowNode->length);
                        callBackFunctions[nowNode->id](nowNode->session, nowNode->id, msgData);


                        long long end = std::chrono::floor<std::chrono::milliseconds>(
                            std::chrono::system_clock::now()
                        ).time_since_epoch().count();

                        metrics.totalProcessingTime += (end - start);
                        metrics.totalProcessed++;
                    }

                    metrics.pendingMessages--;
                }
            }
            return;
        }

        std::shared_ptr<MessageNode> nowNode = nullptr;
        logicSystem->messageNodes.try_dequeue(nowNode);

        if (nowNode != nullptr) {

            if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {
                LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);
            }
            else {
                // 处理消息
                long long start = std::chrono::floor<std::chrono::milliseconds>(
                    std::chrono::system_clock::now()
                ).time_since_epoch().count();

                std::string msgData(nowNode->data, nowNode->length);
                callBackFunctions[nowNode->id](nowNode->session, nowNode->id, msgData);

                long long end = std::chrono::floor<std::chrono::milliseconds>(
                    std::chrono::system_clock::now()
                ).time_since_epoch().count();

                metrics.totalProcessingTime += (end - start);
                metrics.totalProcessed++;
            }

            metrics.pendingMessages--;

        }
    }

    return;
}

void LogicSystem::postMessageToQueue(std::shared_ptr<MessageNode> node) {

	messageNodes.enqueue(node);

	metrics.pendingMessages++;

	int readyIndex;

	if (readyQueue.pop(readyIndex)) {

        if (readyIndex < 0 || readyIndex >= nowSize.load() - 1) return;

		systemCoroutines[readyIndex].handle.resume();

		metrics.busyCoroutines++;

	}

}

void LogicSystem::registerCallBackFunction() {

	callBackFunctions[1001] = std::bind(&LogicSystem::boostAsioTcpSocket, this,
		std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

}

std::vector<std::string> getServers() {

	std::vector<std::string> serverList;

	std::string serverStringList = ConfigMgr::Inst()["Chatservers"]["Name"];

	std::stringstream stream(serverStringList);

	std::string server;

	while (std::getline(stream, server, ',')) {

		serverList.push_back(server);

	}

	return serverList;

}

void LogicSystem::boostAsioTcpSocket(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data) {

	session->send("boostAsioTcpSocket::Coroutine CPlusPlus20", 1001);

}

