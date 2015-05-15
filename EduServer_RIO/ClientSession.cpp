#include "stdafx.h"
#include "Exception.h"
#include "EduServer_RIO.h"
#include "ClientSession.h"
#include "RIOManager.h"
#include "SessionManager.h"


ClientSession::ClientSession() 
: mSocket(NULL), mConnected(0), mRefCount(0)
	, mCircularBuffer(nullptr), mRioBufferId(NULL), mRioBufferPointer(nullptr)
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

bool ClientSession::PostAccept()
{
	OverlappedAcceptContext* acceptContext = new OverlappedAcceptContext(this);
	DWORD bytes = 0;
	acceptContext->mWsaBuf.len = 0;
	acceptContext->mWsaBuf.buf = nullptr;

	mSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);

	if (FALSE == AcceptEx(*GRioManager->GetListenSocket(), mSocket, GRioManager->mAcceptBuf, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, (LPOVERLAPPED)acceptContext))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			delete acceptContext;
			printf_s("AcceptEx Error : %d\n", GetLastError());

			return false;
		}
	}

	return true;
}


void ClientSession::AcceptCompletion()
{
	if (1 == InterlockedExchange(&mConnected, 1))
	{
		/// already exists?
		CRASH_ASSERT(false);
		return;
	}

	bool resultOk = true;
	do
	{
		if (SOCKET_ERROR == setsockopt(mSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)GRioManager->GetListenSocket(), sizeof(SOCKET)))
		{
			printf_s("[DEBUG] SO_UPDATE_ACCEPT_CONTEXT error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		int opt = 1;
		if (SOCKET_ERROR == setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int)))
		{
			printf_s("[DEBUG] TCP_NODELAY error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		int addrlen = sizeof(SOCKADDR_IN);
		if (SOCKET_ERROR == getpeername(mSocket, (SOCKADDR*)&mClientAddr, &addrlen))
		{
			printf_s("[DEBUG] getpeername error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		/// create socket RQ
		/// SEND and RECV within one CQ (you can do with two CQs, seperately)
		/// FYI: RQ will be closed automatically when closesocket()
		mRequestQueue = RIO.RIOCreateRequestQueue(mSocket, MAX_RECV_RQ_SIZE_PER_SOCKET, 1, MAX_SEND_RQ_SIZE_PER_SOCKET, 1,
			GRioManager->GetCompletionQueue(), GRioManager->GetCompletionQueue(), NULL);
		if (mRequestQueue == RIO_INVALID_RQ)
		{
			printf_s("[DEBUG] RIOCreateRequestQueue Error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		HANDLE handle = CreateIoCompletionPort((HANDLE)mSocket, GRioManager->GetIocp(), (ULONG_PTR)this, 0);
		if (handle != GRioManager->GetIocp())
		{
			printf_s("[DEBUG] CreateIoCompletionPort error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

	} while (false);


	if (!resultOk)
	{
		DisconnectRequest(DR_ONCONNECT_ERROR);
		return;
	}

	printf_s("[DEBUG] Client Connected: IP=%s, PORT=%d\n", inet_ntoa(mClientAddr.sin_addr), ntohs(mClientAddr.sin_port));

	if (false == PostRecv())
	{
		printf_s("[DEBUG] PostRecv error: %d\n", GetLastError());
	}
}


void ClientSession::DisconnectRequest(DisconnectReason dr)
{
	/// ÀÌ¹Ì ²÷°å°Å³ª ²÷±â´Â ÁßÀÌ°Å³ª
	if (0 == InterlockedExchange(&mConnected, 0))
		return;

	OverlappedDisconnectContext* context = new OverlappedDisconnectContext(this, dr);

	if (FALSE == DisconnectEx(mSocket, (LPWSAOVERLAPPED)context, TF_REUSE_SOCKET, 0))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			delete context;
			printf_s("ClientSession::DisconnectRequest Error : %d\n", GetLastError());
		}
	}
}

void ClientSession::DisconnectCompletion(DisconnectReason dr)
{
	LINGER lingerOption;
	lingerOption.l_onoff = 1;
	lingerOption.l_linger = 0;

	/// no TCP TIME_WAIT
	if (SOCKET_ERROR == setsockopt(mSocket, SOL_SOCKET, SO_LINGER, (char*)&lingerOption, sizeof(LINGER)))
	{
		printf_s("[DEBUG] setsockopt linger option error: %d\n", GetLastError());
	}

	printf_s("[DEBUG] Client Disconnected: Reason=%d IP=%s, PORT=%d \n", dr, inet_ntoa(mClientAddr.sin_addr), ntohs(mClientAddr.sin_port));

	closesocket(mSocket);

	mConnected = false;
	mSocket = NULL;

	/// release refcount when added at issuing a session
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
		ReleaseRioContext(recvContext);
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
		ReleaseRioContext(sendContext);
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


