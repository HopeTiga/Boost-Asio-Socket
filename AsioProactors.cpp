#include "AsioProactors.h"
#include "AdvancedSystemMonitor.h"


AsioProactors::AsioProactors(size_t minSize, size_t maxSize):minSize(minSize),maxSize(maxSize),nowSize(minSize)
, ioContexts(maxSize),works(maxSize),threads(maxSize), ioPressures(maxSize), isStop(false){
	
	for (int i = 0; i < nowSize; i++) {

		std::unique_ptr<boost::asio::io_context::work> work = std::make_unique<boost::asio::io_context::work>(ioContexts[i]);
		
		works[i] = std::move(work);

		threads[i] = std::thread([this, i]() {

			ioContexts[i].run();

			});

	}
	systemMonitorThread = std::thread([this]() {

		AdvancedSystemMonitor::getInstance()->startMonitoring();

		while (!isStop) {

			double pressures =  AdvancedSystemMonitor::getInstance()->getSystemLoadAverage();

			if (pressures > 0.6) {

				std::lock_guard<std::mutex> lock(mutexs);

				if (this->nowSize == this->maxSize) {

					std::this_thread::sleep_for(updateInterval);

					continue;
				}

				size_t newIndex = this->nowSize.fetch_add(1);

				std::unique_ptr<boost::asio::io_context::work> work = std::make_unique<boost::asio::io_context::work>(ioContexts[newIndex]);

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

				size_t newSize = this->nowSize.fetch_sub(1) - 1;  // 原子性地减少并获取新大小

				size_t indexToRemove = newSize;             // 要移除的索引

				works[indexToRemove]->get_io_context().stop();

				works[indexToRemove].reset();

				this->threads[indexToRemove].join();

			}

			std::this_thread::sleep_for(updateInterval);
		}

		});


}

AsioProactors::~AsioProactors()
{
	stop();
}

void AsioProactors::stop()
{

	if (systemMonitorThread.joinable()) {

		systemMonitorThread.join();

	}

	// 停止系统监控器
	AdvancedSystemMonitor::getInstance()->stopMonitoring();

	for (auto& work : works) {
		//把服务先停止
		work->get_io_context().stop();
		work.reset();
	}

	for (auto& t : threads) {
		if (t.joinable()) {
			t.join();
		}
	}
}

boost::asio::io_context& AsioProactors::getIoComplatePorts()
{

	int balancing = loadBalancing++;

	boost::asio::io_context& context = ioContexts[balancing];

	ioPressures[balancing]++;

	if (loadBalancing == nowSize) {

		loadBalancing = 0;

	}

	return context;
}


