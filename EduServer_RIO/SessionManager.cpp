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

	for (int i = 1; i <= MAX_RIO_THREAD; ++i)
	{
		for (auto it : mFreeSessionList[i] )
		{
			delete it;
		}
	}
}

bool SessionManager::PrepareSessionPool()
{
	CRASH_ASSERT(LIoThreadId == MAIN_THREAD_ID);

	for (int i = 1; i <= MAX_RIO_THREAD; ++i)
	{
		for (int j = 0; j < MAX_CLIENT_PER_RIO_THREAD; ++j)
		{
			ClientSession* client = new ClientSession(i);
			if (false == client->RioInitialize())
				return false;

			mFreeSessionList[i].push_back(client);
		}
	}

	return true;
}

ClientSession* SessionManager::IssueClientSession()
{
	FastSpinlockGuard guard(mLock);

	uint64_t threadId = (mCurrentIssueCount++ % MAX_RIO_THREAD) + 1;
	CRASH_ASSERT(threadId > 0);

	CRASH_ASSERT((mCurrentIssueCount - mCurrentReturnCount) <= (MAX_RIO_THREAD*MAX_CLIENT_PER_RIO_THREAD));

	ClientSession* newClient = mFreeSessionList[threadId].back();
	mFreeSessionList[threadId].pop_back();

	newClient->AddRef();

	mOccupiedSessionList.push_back(newClient);

	return newClient;
}


void SessionManager::ReturnClientSession(ClientSession* client)
{
	FastSpinlockGuard guard(mLock);

	CRASH_ASSERT(client->mSocket == NULL && client->mConnected == false && client->mRefCount == 0);

	uint64_t threadId = (mCurrentReturnCount++ % MAX_RIO_THREAD) + 1;
	CRASH_ASSERT(threadId > 0);

	mOccupiedSessionList.pop_back();

	mFreeSessionList[threadId].push_back(client);
}