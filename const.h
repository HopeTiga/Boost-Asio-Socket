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
	Error_Json = 1001,  //Json��������
	RPCFailed = 1002,  //RPC�������
	VarifyExpired = 1003, //��֤�����
	VarifyCodeErr = 1004, //��֤�����
	UserExist = 1005,       //�û��Ѿ�����
	PasswdErr = 1006,    //�������
	EmailNotMatch = 1007,  //���䲻ƥ��
	PasswdUpFailed = 1008,  //��������ʧ��
	PasswdInvalid = 1009,   //�������ʧ��
	TokenInvalid = 1010,   //TokenʧЧ
	UidInvalid = 1011,  //uid��Ч

	JAVAERROR = 2001,
};


// Defer��
class Defer {
public:
	// ����һ��lambda���ʽ���ߺ���ָ��
	Defer(std::function<void()> func) : func_(func) {}

	// ����������ִ�д���ĺ���
	~Defer() {
		func_();
	}

private:
	std::function<void()> func_;
};

#define MAX_LENGTH  1024*2
//ͷ���ܳ���
#define HEAD_TOTAL_LEN 10
//ͷ��id����
#define HEAD_ID_LEN 2
//ͷ�����ݳ���
#define HEAD_DATA_LEN 8
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000


enum MSG_IDS {
	MSG_CHAT_LOGIN = 1005, //�û���½
	MSG_CHAT_LOGIN_RSP = 1006, //�û���½�ذ�
	ID_SEARCH_USER_REQ = 1007, //�û���������
	ID_SEARCH_USER_RSP = 1008, //�����û��ذ�
	ID_ADD_FRIEND_REQ = 1009, //������Ӻ�������
	ID_CHAT_APPLY_ACCEPT = 1010, //������Ӻ��ѻظ�
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,  //֪ͨ�û���Ӻ�������
	ID_AUTH_FRIEND_REQ = 1013,  //��֤��������
	ID_AUTH_FRIEND_RSP = 1014,  //��֤���ѻظ�
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015, //֪ͨ�û���֤��������
	ID_CHAT_SEND_MESSAGE = 1012, // ������Ϣ
	ID_CHAT_SEND_MESSAGE_RESPONSE = 1016,//������Ϣ�Ļذ�;
	ID_CHAT_SEND_DIR = 1022,//�����ļ���;
	ID_CHAT_SEND_FILE = 1023,//�����ļ���;
	ID_CHAT_SEND_DIR_NOT_FOUND_USER = 1024,//û�ҵ��û��Ļذ�;
	ID_CHAT_SEND_DIR_FOUND_USER = 1025, //�û�����;
	ID_CHAT_SWOP_IP_PORT = 1026,//����IP��ַ;
	ID_CHAT_SEND_REJECT = 1027, //�ܾ��Է������ļ�
	ID_CHAT_FILE_RECEIVE = 1028,//���ջ�ܾ��ļ�;
	ID_CHAT_VIDEO_CALL_FONT_USER_IS_LOGIN = 1029,//��ѯ�û��Ƿ�����;
	ID_CHAT_USER_SOWP_IP = 1030,//����ip;
	ID_CHAT_USER_NOT_LOGIN = 1031,//�û�������
	ID_CHAT_VIDEO_CALL_FONT_USER_IP_RESPONSE = 1032,//����IP
	ID_CHAT_VIDEO_CALL_FONT_USER_REJECT = 1033,//�ܾ�ͨ��;
	ID_CHAT_VIDEO_CALL_FINISHED = 1034,//����ͨ��
	ID_CHAT_GROUP_SEND_MESSAGE = 1045,//����Ⱥ����Ϣ;
	ID_CHAT_CHANGE_GROUP_ENCRYPT = 1046,//�����е�����Ⱥ�ѵĸ�Ⱥ��Ϣ����״̬���иı�!
	ID_CHAT_SYSTEM_ERROR = 1047,//ϵͳ����;
	ID_CHAT_HANDLE_GROUP_JOIN = 1050, //������Ⱥ����;
	ID_CHAT_HANDLE_GROUP_JOIN_RESULT = 1051,//������Ⱥ����Ļص�;
	ID_CHAT_SEND_GROUP_JOIN = 1052,//������Ⱥ����;
	ID_CHAT_GROUP_JOIN_RESULT = 1053, //��Ⱥ���봦����
	ID_CHAT_UPDATE_GROUP_USER_ROLE = 1055, //������ȡ������Ա;
	ID_CHAT_KICK_GROUP_USER = 1056,//����;
	ID_CHAT_EXIT_GROUP = 1057 ,//�˳�Ⱥ��;
	ID_CHAT_GROUP_START_MEETING = 1058 , //��ʼ��Ƶ����;
	ID_CHAT_INIT_GROUP_MEETING = 1059 , //��ȡȺ��Ƶ����������Ϣ;
	ID_CHAT_GROUP_JOIN_MEETING = 1060 , //������Ƶ����;
	ID_CHAT_GROUP_EXIT_MEETING = 1061,//�˳���Ƶ����;
	ID_CHAT_ESC_GROUP_MEETING = 1062, //�Ƿ��˳�����;
};

#define USERIPPREFIX  "uip_"
#define USERTOKENPREFIX  "utoken_"
#define IPCOUNTPREFIX  "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define LOGIN_COUNT  "logincount"
#define NAME_INFO  "nameinfo_"