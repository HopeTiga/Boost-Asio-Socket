#pragma once
#include<iostream>
#include<string>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <thread>
#include <winhttp.h>
#include <vector>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <condition_variable>
#include <atomic>
#include <map>
#include "AsioIOServicePool.h"
#include "ConfigMgr.h"


#define MAX_LENGTH  1024*2

#define HEAD_TOTAL_LEN 10

#define HEAD_ID_LEN 2

#define HEAD_DATA_LEN 8
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000

