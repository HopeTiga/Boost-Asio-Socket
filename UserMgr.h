#pragma once
#include "const.h"

class CSession;

class UserMgr : public Singleton<UserMgr>
{
	friend class Singleton<UserMgr>;

public:
	~UserMgr();

	std::shared_ptr<CSession> getSession(int uid);

	void setSession(int uid ,std::shared_ptr<CSession> session);

	void removeSession(int uid);

	std::unordered_map<int64_t, std::map<int, std::shared_ptr<CSession>>> groupMeetingMap;

private:

	std::unordered_map<int, std::shared_ptr<CSession>> userSessionMap;

	std::mutex mutexs;

	UserMgr();
};

