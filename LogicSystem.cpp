#include "LogicSystem.h"
#include <chrono>
#include "Utils.h"

LogicSystem::LogicSystem(size_t minSize, size_t maxSize) :minSize(minSize), maxSize(maxSize), nowSize(minSize), isStop(false), threads(maxSize), readyQueue(nowSize),ioContexts(nowSize),works(nowSize), channels(nowSize)
 {
	registerCallBackFunction();

}

void LogicSystem::initializeThreads() {

    for (int i = 0; i < nowSize; i++) {

        auto work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            boost::asio::make_work_guard(ioContexts[i])
        );

        works[i] = std::move(work);

        threads[i] = std::move(std::thread([this, i]() {
            ioContexts[i].run();
            }));

        channels[i] = std::make_unique<boost::asio::experimental::concurrent_channel<void(boost::system::error_code)>>(ioContexts[i], 1);

    }

    for (int i = 0; i < nowSize; i++) {

		auto self = shared_from_this();

        boost::asio::co_spawn(ioContexts[i], [self,i]() -> boost::asio::awaitable<void> {

            for (;;) {

                std::shared_ptr<MessageNode> nowNode = nullptr;

                while (self->messageNodes.try_dequeue(nowNode)) {

                    if (nowNode != nullptr && nowNode->session != nullptr) {

                        if (self->callBackFunctions.find(nowNode->id) == self->callBackFunctions.end()) {

                            LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);

                        }
                        else {

                            self->callBackFunctions[nowNode->id](nowNode->session, nowNode->id, nowNode->data);

                        }

                    }
                }

                if (!self->isStop) {

					self->readyQueue.push(i);

                    co_await self->channels[i]->async_receive(boost::asio::use_awaitable);
                }
                else {

                    std::shared_ptr<MessageNode> nowNode = nullptr;

                    while (self->messageNodes.try_dequeue(nowNode)) {

                        if (nowNode != nullptr && nowNode->session != nullptr) {

                            if (self->callBackFunctions.find(nowNode->id) == self->callBackFunctions.end()) {

                                LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);

                            }
                            else {

                                self->callBackFunctions[nowNode->id](nowNode->session, nowNode->id, nowNode->data);

                            }

                            nowNode = nullptr;

                        }
                    }
                    co_return;

                }

            }

            co_return;

            }, [this](std::exception_ptr p) {
                if (p) {
                    try {

                        std::rethrow_exception(p);

                    }
                    catch (const std::exception& e) {

                        LOG_ERROR("LogicSystem coroutine std::exception: %s", e.what());
                    }
                }
                });
    }

	metricsThread = std::move(std::thread([this]() {
		
		while (!isStop) {

            if (this->messageNodes.size_approx() > 0) pressuresCount++;

            LOG_INFO("LogicSystem: Monitoring system Threads: %u", nowSize.load());

			LOG_INFO("LogicSystem: Message Pressure: %0.2f", pressuresCount.load());

			if (pressuresCount > 3) {

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

				pressuresCount.store(0); // 重置压力计数器
			}

			std::this_thread::sleep_for(updateInterval);

		}

		}));


}

LogicSystem::~LogicSystem() {

	isStop = true;

	for (auto& thread : threads) {

		if (thread.joinable()) {

			thread.join();

		}
	}

}


void LogicSystem::processMessageTemporary(std::shared_ptr<LogicSystem> logicSystem) {
    // 记录最后活动时间，初始为当前时间
    auto lastActivityTime = std::chrono::steady_clock::now();

    const auto idleTimeout = std::chrono::seconds(60); // 60秒超时

    const auto checkInterval = std::chrono::milliseconds(100); // 检查间隔

    for (;;) {
        // 检查停止标志
        if (isStop) {

            std::shared_ptr<MessageNode> nowNode = nullptr;
            // 处理完剩余所有消息后退出
            while (logicSystem->messageNodes.try_dequeue(nowNode)) {

                if (nowNode != nullptr && nowNode->session != nullptr) {

                    if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {

                        LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);

                    }
                    else {

                        callBackFunctions[nowNode->id](nowNode->session, nowNode->id, nowNode->data);


                    }
                }

                nowNode = nullptr;

                this->nowSize.fetch_sub(1);

                return;
            }
            // 尝试从队列中获取消息
            
        }

        std::shared_ptr<MessageNode> nowNode = nullptr;

        while (logicSystem->messageNodes.try_dequeue(nowNode)) {
            // 成功获取到消息，更新最后活动时间
            lastActivityTime = std::chrono::steady_clock::now();

            if (nowNode != nullptr && nowNode->session != nullptr) {

                if (callBackFunctions.find(nowNode->id) == callBackFunctions.end()) {

                    LOG_WARNING("The MessageID %u has no corresponding CallBackFunctions", nowNode->id);
                }
                else {

                    callBackFunctions[nowNode->id](nowNode->session, nowNode->id, nowNode->data);

                    long long end = std::chrono::floor<std::chrono::milliseconds>(
                        std::chrono::system_clock::now()
                    ).time_since_epoch().count();

                }
            }
        }
        
        auto currentTime = std::chrono::steady_clock::now();

        auto idleDuration = currentTime - lastActivityTime;

        if (idleDuration >= idleTimeout) {
            // 空闲时间超过60秒，退出
            this->nowSize.fetch_sub(1);

            return;
        }

    }
}

void LogicSystem::postMessageToQueue(std::shared_ptr<MessageNode> node) {

	messageNodes.enqueue(node);

	int readyIndex = -1;

	if (readyQueue.pop(readyIndex)) {

        if (readyIndex == -1) return;

        channels[readyIndex]->try_send(boost::system::error_code{});

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

    session->writeAsync("boostAsioTcpSocket::Coroutine CPlusPlus20", 1001);

}

