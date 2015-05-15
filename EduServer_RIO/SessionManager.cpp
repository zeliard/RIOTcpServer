#include "stdafx.h"
#include "Exception.h"
#include "FastSpinlock.h"
#include "EduServer_RIO.h"
#include "ClientSession.h"
#include "SessionManager.h"

SessionManager* GSessionManager = nullptr;

SessionManager::~SessionManager()
{
	for (auto it : mOccupiedSessionList)
	{
		delete it;
	}

	for (auto it : mFreeSessionList )
	{
		delete it;
	}

}

bool SessionManager::PrepareSessionPool()
{
	for (int j = 0; j < MAX_CLIENT; ++j)
	{
		ClientSession* client = new ClientSession;
		if (false == client->RioInitialize())
			return false;

		mFreeSessionList.push_back(client);
	}

	return true;
}

ClientSession* SessionManager::IssueClientSession()
{
	FastSpinlockGuard guard(mLock);



	CRASH_ASSERT((mCurrentIssueCount - mCurrentReturnCount) <= MAX_CLIENT);

	ClientSession* newClient = mFreeSessionList.back();
	mFreeSessionList.pop_back();

	newClient->AddRef();

	mOccupiedSessionList.push_back(newClient);

	return newClient;
}


void SessionManager::ReturnClientSession(ClientSession* client)
{
	FastSpinlockGuard guard(mLock);

	CRASH_ASSERT(client->mSocket == NULL && client->mConnected == false && client->mRefCount == 0);

	mOccupiedSessionList.remove(client);

	mFreeSessionList.push_back(client);
}