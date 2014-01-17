#include "stdafx.h"
#include "Exception.h"
#include "EduServer_RIO.h"
#include "ClientSession.h"
#include "RIOManager.h"
#include "SessionManager.h"


ClientSession::ClientSession(int threadId) 
: mSocket(NULL), mConnected(false), mRefCount(0)
	, mCircularBuffer(nullptr), mRioBufferId(NULL), mRioBufferPointer(nullptr), mRioThreadId(threadId)
{
	memset(&mClientAddr, 0, sizeof(SOCKADDR_IN));
}

ClientSession::~ClientSession()
{
	RIO.RIODeregisterBuffer(mRioBufferId);
	VirtualFreeEx(GetCurrentProcess(), mRioBufferPointer, 0, MEM_RELEASE);
	delete mCircularBuffer;
}


bool ClientSession::RioInitialize()
{
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	const unsigned __int64 granularity = systemInfo.dwAllocationGranularity; ///< maybe 64K

	CRASH_ASSERT(SESSION_BUFFER_SIZE % granularity == 0);

	mRioBufferPointer = reinterpret_cast<char*>(VirtualAllocEx(GetCurrentProcess(), 0, SESSION_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (mRioBufferPointer == nullptr)
	{
		printf_s("[DEBUG] VirtualAllocEx Error: %d\n", GetLastError());
		return false;
	}

	mCircularBuffer = new CircularBuffer(mRioBufferPointer, SESSION_BUFFER_SIZE);

	mRioBufferId = RIO.RIORegisterBuffer(mRioBufferPointer, SESSION_BUFFER_SIZE);

	if (mRioBufferId == RIO_INVALID_BUFFERID)
	{
		printf_s("[DEBUG] RIORegisterBuffer Error: %d\n", GetLastError());
		return false;
	}

	return true;
}

bool ClientSession::OnConnect(SOCKET socket, SOCKADDR_IN* addr)
{
	FastSpinlockGuard criticalSection(mSessionLock);

	CRASH_ASSERT(LIoThreadId == MAIN_THREAD_ID);

	mSocket = socket;

	/// make socket non-blocking
	u_long arg = 1 ;
	ioctlsocket(mSocket, FIONBIO, &arg) ;

	/// turn off nagle
	int opt = 1 ;
	setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int)) ;

	/// create socket RQ
	/// SEND and RECV within one CQ (you can do with two CQs, seperately)
	mRequestQueue = RIO.RIOCreateRequestQueue(mSocket, MAX_RECV_RQ_SIZE_PER_SOCKET, 1, MAX_SEND_RQ_SIZE_PER_SOCKET, 1,
		GRioManager->GetCompletionQueue(mRioThreadId), GRioManager->GetCompletionQueue(mRioThreadId), NULL);
	if (mRequestQueue == RIO_INVALID_RQ)
	{
		printf_s("[DEBUG] RIOCreateRequestQueue Error: %d\n", GetLastError());
		return false ;
	}

	memcpy(&mClientAddr, addr, sizeof(SOCKADDR_IN));
	mConnected = true ;

	printf_s("[DEBUG] Client Connected: IP=%s, PORT=%d\n", inet_ntoa(mClientAddr.sin_addr), ntohs(mClientAddr.sin_port));

	return PostRecv() ;
}

void ClientSession::Disconnect(DisconnectReason dr)
{
	FastSpinlockGuard criticalSection(mSessionLock);

	if ( !IsConnected() )
		return ;
	
	LINGER lingerOption ;
	lingerOption.l_onoff = 1;
	lingerOption.l_linger = 0;

	/// no TCP TIME_WAIT
	if (SOCKET_ERROR == setsockopt(mSocket, SOL_SOCKET, SO_LINGER, (char*)&lingerOption, sizeof(LINGER)) )
	{
		printf_s("[DEBUG] setsockopt linger option error: %d\n", GetLastError());
	}

	printf_s("[DEBUG] Client Disconnected: Reason=%d IP=%s, PORT=%d \n", dr, inet_ntoa(mClientAddr.sin_addr), ntohs(mClientAddr.sin_port));
	
	closesocket(mSocket) ;

	mConnected = false ;
	mSocket = NULL;

	ReleaseRef();
}



bool ClientSession::PostRecv()
{
	if (!IsConnected())
		return false;

	if (0 == mCircularBuffer->GetFreeSpaceSize())
		return false;

	RioIoContext* recvContext = new RioIoContext(this, IO_RECV);

	recvContext->BufferId = mRioBufferId;
	recvContext->Length = static_cast<ULONG>(mCircularBuffer->GetFreeSpaceSize());
	recvContext->Offset = mCircularBuffer->GetWritableOffset();
	
	DWORD recvbytes = 0;
	DWORD flags = 0;

	/// start async recv
	if (!RIO.RIOReceive(mRequestQueue, (PRIO_BUF)recvContext, 1, flags, recvContext))
	{
		printf_s("[DEBUG] RIOReceive error: %d\n", GetLastError());
		ReleaseContext(recvContext);
		return false;
	}

	return true;
}

void ClientSession::RecvCompletion(DWORD transferred)
{
	mCircularBuffer->Commit(transferred);
}

bool ClientSession::PostSend()
{
	if (!IsConnected())
		return false;
	
	if ( 0 == mCircularBuffer->GetContiguiousBytes() )
		return true;

	RioIoContext* sendContext = new RioIoContext(this, IO_SEND);

	sendContext->BufferId = mRioBufferId;
	sendContext->Length = static_cast<ULONG>(mCircularBuffer->GetContiguiousBytes()); 
	sendContext->Offset = mCircularBuffer->GetReadableOffset();

	DWORD sendbytes = 0;
	DWORD flags = 0;

	/// start async send
	if (!RIO.RIOSend(mRequestQueue, (PRIO_BUF)sendContext, 1, flags, sendContext))
	{
		printf_s("[DEBUG] RIOSend error: %d\n", GetLastError());
		ReleaseContext(sendContext);
		return false;
	}
	
	return true;
}

void ClientSession::SendCompletion(DWORD transferred)
{
	mCircularBuffer->Remove(transferred);
}


void ClientSession::AddRef()
{
	CRASH_ASSERT(InterlockedIncrement(&mRefCount) > 0);
}

void ClientSession::ReleaseRef()
{
	long ret = InterlockedDecrement(&mRefCount);
	CRASH_ASSERT(ret >= 0);
	
	if (ret == 0)
	{
		GSessionManager->ReturnClientSession(this);
	}
}


