#pragma once
#include "Singleton.h"
#include <memory>
#include <queue>
#include <atomic>
#include <vector>
#include <iostream>
#include <mutex>
#include <functional>
#include <boost/filesystem.hpp>
#include <boost/asio.hpp>

extern class CSession;

class SessionSendThread :public Singleton<SessionSendThread>
{
	friend class Singleton<SessionSendThread>;
public:

	~SessionSendThread();

	SessionSendThread& operator=(SessionSendThread& sessionThread) = delete;

	SessionSendThread(SessionSendThread& session) = delete;

	template<class Func,class... Args>
	void commitTask(Func && func,Args&&... args)
	{

		std::shared_ptr<std::function<void()>> function = std::make_shared<std::function<void()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

		{

			std::lock_guard<std::mutex> lock(mutexs);

			task_queues.emplace(function);

			condition.notify_one();

			threadCount--;

		}

	}

	std::mutex& getMutex();

private:
	SessionSendThread(size_t size = std::thread::hardware_concurrency() * 2);

	void stop();

	std::mutex mutexs;

	std::queue<std::shared_ptr<std::function<void()>>> task_queues;

	std::vector<std::thread> threadPools;

	size_t size;

	std::atomic<int> threadCount;

	std::condition_variable condition;

	std::atomic<bool> isStop;

	std::mutex mutexNumber;
};

