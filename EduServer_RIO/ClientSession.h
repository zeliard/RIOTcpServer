#pragma once
#include "ObjectPool.h"
#include "CircularBuffer.h"
#include "RioContext.h"

class SessionManager;

class ClientSession
{
public:
	ClientSession(int threadId);
	~ClientSession();

	bool	OnConnect(SOCKET socket, SOCKADDR_IN* addr);
	bool	IsConnected() const { return mConnected; }
	int		GetRioThreadId() const { return mRioThreadId; }


	bool	PostRecv();
	void	RecvCompletion(DWORD transferred);

	bool	PostSend();
	void	SendCompletion(DWORD transferred);
	
	void	Disconnect(DisconnectReason dr);
	
	void	AddRef();
	void	ReleaseRef();

	

private:
	bool	RioInitialize();

	char*			mRioBufferPointer;
	RIO_BUFFERID	mRioBufferId;
	CircularBuffer* mCircularBuffer;

	RIO_RQ			mRequestQueue;

private:
	bool			mConnected ;
	int				mRioThreadId;

	SOCKET			mSocket ;
	SOCKADDR_IN		mClientAddr ;
		
	FastSpinlock	mSessionLock;

	volatile long	mRefCount;

	friend class SessionManager;
} ;



