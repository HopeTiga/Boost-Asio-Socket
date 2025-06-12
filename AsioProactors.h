#pragma once
#include<boost/asio.hpp>
#include <memory>
#include <mutex>
#include <thread>

class AsioProactors {

public:
	static AsioProactors* getInstance() {
		static AsioProactors instance;
		return &instance;
	}

	~AsioProactors();

	void stop();

	AsioProactors(const AsioProactors& asioProactors) = delete;

	AsioProactors& operator=(const AsioProactors& asioProactors) = delete;

	boost::asio::io_context& getIoComplatePorts();

private:

	AsioProactors(size_t minSize = std::thread::hardware_concurrency() * 2,size_t maxSize = std::thread::hardware_concurrency() * 4);

	std::vector<boost::asio::io_context> ioContexts;

	std::vector<std::unique_ptr<boost::asio::io_context::work>> works;

	std::vector<std::thread> threads;

	std::vector<std::atomic<size_t>> ioPressures;

	std::mutex mutexs;

	size_t minSize;

	size_t maxSize;

	std::atomic<size_t> nowSize;

	std::atomic<size_t> loadBalancing = 0;

	std::thread systemMonitorThread;

	std::chrono::milliseconds updateInterval{ 30000 };

	std::atomic<bool> isStop;
};

