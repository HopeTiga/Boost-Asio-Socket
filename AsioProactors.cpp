#include "AsioProactors.h"
#include "AdvancedSystemMonitor.h"
#include <iostream>
#include "Utils.h"

AsioProactors::AsioProactors(size_t minSize, size_t maxSize) :minSize(minSize), maxSize(maxSize), nowSize(minSize)
, ioContexts(maxSize), works(maxSize), threads(maxSize), ioPressures(maxSize), isStop(false) {

	for (int i = 0; i < nowSize; i++) {
		// 使用新的 work guard API
		auto work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
			boost::asio::make_work_guard(ioContexts[i])
		);

		works[i] = std::move(work);
		threads[i] = std::thread([this, i]() {
			ioContexts[i].run();
			});
	}
	systemMonitorThread = std::thread([this]() {
		AdvancedSystemMonitor::getInstance()->startMonitoring();
		while (!isStop) {
			double pressures = AdvancedSystemMonitor::getInstance()->getSystemLoadAverage();
			LOG_INFO("AsioProactors: Monitoring system Threads: %d", nowSize.load());
			LOG_INFO("AsioProactors: System Load Average: %0.2f", pressures);
			if (pressures > 0.6) {
				std::lock_guard<std::mutex> lock(mutexs);
				if (this->nowSize == this->maxSize) {
					std::this_thread::sleep_for(updateInterval);
					continue;
				}
				size_t newIndex = this->nowSize.fetch_add(1);

				// 使用新的 work guard API
				auto work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
					boost::asio::make_work_guard(ioContexts[newIndex])
				);

				works[newIndex] = std::move(work);
				threads[newIndex] = std::move(std::thread([this, newIndex]() {
					ioContexts[newIndex].run();
					}));
			}
			else if (pressures < 0.3) {
				std::lock_guard<std::mutex> lock(mutexs);
				if (this->nowSize == this->minSize) {
					std::this_thread::sleep_for(updateInterval);
					continue;
				}
				size_t newSize = this->nowSize.fetch_sub(1) - 1;
				size_t indexToRemove = newSize;

				// 停止 io_context 并重置 work guard
				ioContexts[indexToRemove].stop();
				works[indexToRemove].reset();
				this->threads[indexToRemove].join();
			}
			std::this_thread::sleep_for(updateInterval);
		}
		});
}

AsioProactors::~AsioProactors() {
	stop();
}

void AsioProactors::stop() {
	isStop = true;

	if (systemMonitorThread.joinable()) {
		systemMonitorThread.join();
	}
	// 停止系统监控
	AdvancedSystemMonitor::getInstance()->stopMonitoring();

	for (auto& work : works) {
		// 重置 work guard，这会让 io_context 停止运行
		if (work) {
			work.reset();
		}
	}

	// 明确停止所有 io_context
	for (auto& context : ioContexts) {
		context.stop();
	}

	for (auto& t : threads) {
		if (t.joinable()) {
			t.join();
		}
	}
}

boost::asio::io_context& AsioProactors::getIoComplatePorts() {
	int balancing = loadBalancing++;
	boost::asio::io_context& context = ioContexts[balancing % nowSize];
	ioPressures[balancing % nowSize]++;
	if (loadBalancing >= nowSize) {
		loadBalancing = 0;
	}
	return context;
}