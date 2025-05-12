#pragma once
#include "const.h"
#include"winhttp.h"
#include "Singleton.h"
#pragma comment(lib,"winhttp.lib")
class HttpUtils : public Singleton<HttpUtils>
{
	friend class Singleton<HttpUtils>;
public:
	~HttpUtils();

	HttpUtils(const HttpUtils&) = delete;
	HttpUtils& operator=(const HttpUtils&) = delete;

	Json::Value doPost(std::string url, Json::Value json);

private:
	HttpUtils();

};

