#include "stdafx.h"
#include "Exception.h"
#include "FastSpinlock.h"
#include "EduServer_RIO.h"
#include "ClientSession.h"
#include "SessionManager.h"

SessionManager* GSessionManager = nullptr;

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


bool SessionManager::AcceptSessions()
{
	FastSpinlockGuard guard(mLock);

	while (mCurrentIssueCount - mCurrentReturnCount < MAX_CLIENT)
	{
		ClientSession* newClient = mFreeSessionList.back();
		mFreeSessionList.pop_back();

		++mCurrentIssueCount;

		newClient->AddRef(); ///< refcount +1 for issuing 

		if (false == newClient->PostAccept())
			return false;
	}

	return true;
}


void SessionManager::ReturnClientSession(ClientSession* client)
{
	FastSpinlockGuard guard(mLock);

	CRASH_ASSERT(client->mSocket == NULL && client->mConnected == 0 && client->mRefCount == 0);

	mFreeSessionList.push_back(client);
}