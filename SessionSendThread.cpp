#include "SessionSendThread.h"
#include "CSession.h"

SessionSendThread::~SessionSendThread()
{
	stop();
}

SessionSendThread::SessionSendThread(size_t size):size(size),threadCount(size), isStop(false)
{

	for (int i = 0; i < size; i++) {

		threadPools.push_back(std::move(std::thread([this]() {

			for (;;) {

				if (isStop.load()) return;

				std::shared_ptr<std::function<void()>> func;

				{
					std::unique_lock<std::mutex> lock(mutexs);

					condition.wait(lock, [this]() {

						bool flag = !this->task_queues.empty() && !this->isStop.load();

						if (!flag && (this->threadCount.load() < this->size)) {

							this->threadCount++;

						}

						return flag;

						});

					if (!task_queues.empty()) {

						func = task_queues.front();

						task_queues.pop();

					}

				}
				
				(*func)();

			}
			})));
	}

}


void SessionSendThread::stop()
{
	if (!isStop.load()) {

		isStop.store(true);

	}

	condition.notify_all();

	for (auto& thread : threadPools) {

		if (thread.joinable()) {

			thread.join();

		}

	}
}



std::mutex& SessionSendThread::getMutex()
{
	// TODO: 在此处插入 return 语句
	return mutexs;
}
