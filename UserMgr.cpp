#include "UserMgr.h"
#include "CSession.h"

UserMgr::~UserMgr() {
	userSessionMap.clear();
}

std::shared_ptr<CSession> UserMgr::getSession(int uid) {
	std::lock_guard<std::mutex> lock(mutexs);

	if (userSessionMap.find(uid) == userSessionMap.end()) return nullptr;

	return userSessionMap[uid];
}

void UserMgr::setSession(int uid, std::shared_ptr<CSession> session) {

	std::lock_guard<std::mutex> lock(mutexs);

	userSessionMap[uid] = session;

}

void UserMgr::removeSession(int uid) {

	std::lock_guard<std::mutex> lock(mutexs);

	userSessionMap.erase(uid);

}

UserMgr::UserMgr() {

}