#pragma once
#include <list>
#include <WinSock2.h>
#include "FastSpinlock.h"

class ClientSession;

class SessionManager
{
public:
	SessionManager() : mCurrentIssueCount(0), mCurrentReturnCount(0)
	{}
	~SessionManager();

	bool PrepareSessionPool();

	ClientSession* IssueClientSession();

	void ReturnClientSession(ClientSession* client);


private:
	typedef std::list<ClientSession*> ClientList;
	ClientList	mFreeSessionList[MAX_RIO_THREAD+1];
	ClientList	mOccupiedSessionList;

	FastSpinlock mLock;

	uint64_t mCurrentIssueCount;
	uint64_t mCurrentReturnCount;

};

extern SessionManager* GSessionManager;
