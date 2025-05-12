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
#include "HttpUtils.h"


namespace beast = boost::beast;

namespace http = beast::http;

namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;


enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,  //Json解析错误
	RPCFailed = 1002,  //RPC请求错误
	VarifyExpired = 1003, //验证码过期
	VarifyCodeErr = 1004, //验证码错误
	UserExist = 1005,       //用户已经存在
	PasswdErr = 1006,    //密码错误
	EmailNotMatch = 1007,  //邮箱不匹配
	PasswdUpFailed = 1008,  //更新密码失败
	PasswdInvalid = 1009,   //密码更新失败
	TokenInvalid = 1010,   //Token失效
	UidInvalid = 1011,  //uid无效

	JAVAERROR = 2001,
};


// Defer类
class Defer {
public:
	// 接受一个lambda表达式或者函数指针
	Defer(std::function<void()> func) : func_(func) {}

	// 析构函数中执行传入的函数
	~Defer() {
		func_();
	}

private:
	std::function<void()> func_;
};

#define MAX_LENGTH  1024*2
//头部总长度
#define HEAD_TOTAL_LEN 10
//头部id长度
#define HEAD_ID_LEN 2
//头部数据长度
#define HEAD_DATA_LEN 8
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000


enum MSG_IDS {
	MSG_CHAT_LOGIN = 1005, //用户登陆
	MSG_CHAT_LOGIN_RSP = 1006, //用户登陆回包
	ID_SEARCH_USER_REQ = 1007, //用户搜索请求
	ID_SEARCH_USER_RSP = 1008, //搜索用户回包
	ID_ADD_FRIEND_REQ = 1009, //申请添加好友请求
	ID_CHAT_APPLY_ACCEPT = 1010, //申请添加好友回复
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,  //通知用户添加好友申请
	ID_AUTH_FRIEND_REQ = 1013,  //认证好友请求
	ID_AUTH_FRIEND_RSP = 1014,  //认证好友回复
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015, //通知用户认证好友申请
	ID_CHAT_SEND_MESSAGE = 1012, // 发送信息
	ID_CHAT_SEND_MESSAGE_RESPONSE = 1016,//发送信息的回包;
	ID_CHAT_SEND_DIR = 1022,//发送文件夹;
	ID_CHAT_SEND_FILE = 1023,//发送文件夹;
	ID_CHAT_SEND_DIR_NOT_FOUND_USER = 1024,//没找到用户的回包;
	ID_CHAT_SEND_DIR_FOUND_USER = 1025, //用户在线;
	ID_CHAT_SWOP_IP_PORT = 1026,//交换IP地址;
	ID_CHAT_SEND_REJECT = 1027, //拒绝对方发送文件
	ID_CHAT_FILE_RECEIVE = 1028,//接收或拒绝文件;
	ID_CHAT_VIDEO_CALL_FONT_USER_IS_LOGIN = 1029,//查询用户是否在线;
	ID_CHAT_USER_SOWP_IP = 1030,//交换ip;
	ID_CHAT_USER_NOT_LOGIN = 1031,//用户不在线
	ID_CHAT_VIDEO_CALL_FONT_USER_IP_RESPONSE = 1032,//返回IP
	ID_CHAT_VIDEO_CALL_FONT_USER_REJECT = 1033,//拒绝通话;
	ID_CHAT_VIDEO_CALL_FINISHED = 1034,//结束通话
	ID_CHAT_GROUP_SEND_MESSAGE = 1045,//发送群聊消息;
	ID_CHAT_CHANGE_GROUP_ENCRYPT = 1046,//将所有的在线群友的该群消息加密状态进行改变!
	ID_CHAT_SYSTEM_ERROR = 1047,//系统错误;
	ID_CHAT_HANDLE_GROUP_JOIN = 1050, //处理入群申请;
	ID_CHAT_HANDLE_GROUP_JOIN_RESULT = 1051,//处理入群申请的回调;
	ID_CHAT_SEND_GROUP_JOIN = 1052,//发送入群申请;
	ID_CHAT_GROUP_JOIN_RESULT = 1053, //入群申请处理结果
	ID_CHAT_UPDATE_GROUP_USER_ROLE = 1055, //提升和取消管理员;
	ID_CHAT_KICK_GROUP_USER = 1056,//踢人;
	ID_CHAT_EXIT_GROUP = 1057 ,//退出群聊;
	ID_CHAT_GROUP_START_MEETING = 1058 , //开始视频会议;
	ID_CHAT_INIT_GROUP_MEETING = 1059 , //获取群视频会议流的信息;
	ID_CHAT_GROUP_JOIN_MEETING = 1060 , //加入视频会议;
	ID_CHAT_GROUP_EXIT_MEETING = 1061,//退出视频会议;
	ID_CHAT_ESC_GROUP_MEETING = 1062, //是否退出会议;
};

#define USERIPPREFIX  "uip_"
#define USERTOKENPREFIX  "utoken_"
#define IPCOUNTPREFIX  "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define LOGIN_COUNT  "logincount"
#define NAME_INFO  "nameinfo_"