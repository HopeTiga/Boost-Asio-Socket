#include "AsioProactors.h"


AsioProactors::AsioProactors(size_t minSize, size_t maxSize):minSize(minSize),maxSize(maxSize),nowSize(minSize)
, ioContexts(minSize),works(minSize),threads(minSize), ioPressures(minSize){
	
	for (int i = 0; i < nowSize; i++) {

		std::unique_ptr<boost::asio::io_context::work> work = std::make_unique<boost::asio::io_context::work>(ioContexts[i]);
		
		works[i] = std::move(work);

		threads[i] = std::thread([this, i]() {

			ioContexts[i].run();

			});

	}

}

AsioProactors::~AsioProactors()
{
	stop();
}

void AsioProactors::stop()
{
	for (auto& work : works) {
		//把服务先停止
		work->get_io_context().stop();
		work.reset();
	}

	for (auto& t : threads) {
		t.join();
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


