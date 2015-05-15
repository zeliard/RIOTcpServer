#pragma once
#include "CircularBuffer.h"
#include "RioContext.h"

class SessionManager;

class ClientSession
{
public:
	ClientSession();
	~ClientSession();

	bool	IsConnected() const { return !!mConnected; }

	bool	PostAccept();
	void	AcceptCompletion();

	void	DisconnectRequest(DisconnectReason dr);
	void	DisconnectCompletion(DisconnectReason dr);

	bool	PostRecv();
	void	RecvCompletion(DWORD transferred);

	bool	PostSend();
	void	SendCompletion(DWORD transferred);
	
	void	AddRef();
	void	ReleaseRef();

private:
	bool	RioInitialize();

	char*			mRioBufferPointer;
	RIO_BUFFERID	mRioBufferId;
	CircularBuffer* mCircularBuffer;

	RIO_RQ			mRequestQueue;

private:
	
	SOCKET			mSocket ;
	SOCKADDR_IN		mClientAddr ;
		
	volatile long	mConnected;
	volatile long	mRefCount;

	friend class SessionManager;
} ;



