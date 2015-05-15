#include "stdafx.h"
#include "EduServer_RIO.h"
#include "RioContext.h"
#include "RIOManager.h"
#include "ClientSession.h"
#include "SessionManager.h"


RIO_EXTENSION_FUNCTION_TABLE RIOManager::mRioFunctionTable = { 0, };
RIO_CQ RIOManager::mRioCompletionQueue(nullptr);

LPFN_DISCONNECTEX RIOManager::mFnDisconnectEx = nullptr;
LPFN_ACCEPTEX RIOManager::mFnAcceptEx = nullptr;
char RIOManager::mAcceptBuf[64] = { 0, };

BOOL DisconnectEx(SOCKET hSocket, LPOVERLAPPED lpOverlapped, DWORD dwFlags, DWORD reserved)
{
	return RIOManager::mFnDisconnectEx(hSocket, lpOverlapped, dwFlags, reserved);
}

BOOL AcceptEx(SOCKET sListenSocket, SOCKET sAcceptSocket, PVOID lpOutputBuffer, DWORD dwReceiveDataLength,
	DWORD dwLocalAddressLength, DWORD dwRemoteAddressLength, LPDWORD lpdwBytesReceived, LPOVERLAPPED lpOverlapped)
{
	return RIOManager::mFnAcceptEx(sListenSocket, sAcceptSocket, lpOutputBuffer, dwReceiveDataLength,
		dwLocalAddressLength, dwRemoteAddressLength, lpdwBytesReceived, lpOverlapped);
}


RIOManager* GRioManager = nullptr;

RIOManager::RIOManager() : mListenSocket(NULL), mIocp(NULL)
{
}


RIOManager::~RIOManager()
{
	CloseHandle(mIocp);

	/// winsock finalizing
	WSACleanup();
}


bool RIOManager::Initialize()
{
	/// winsock initializing
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	/// create TCP socket with RIO mode
	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO | WSA_FLAG_OVERLAPPED);
	if (mListenSocket == INVALID_SOCKET)
		return false;

	mIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL == mIocp)
		return false;

	HANDLE handle = CreateIoCompletionPort((HANDLE)mListenSocket, mIocp, 0, 0);
	if (handle != mIocp)
	{
		printf_s("[DEBUG] listen socket IOCP register error: %d\n", GetLastError());
		return false;
	}

	int opt = 1;
	setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(int));

	/// bind
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(LISTEN_PORT);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (SOCKET_ERROR == bind(mListenSocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr)))
		return false;

	GUID guidDisconnectEx = WSAID_DISCONNECTEX;
	DWORD bytes = 0;
	if (SOCKET_ERROR == WSAIoctl(mListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidDisconnectEx, sizeof(GUID), &mFnDisconnectEx, sizeof(LPFN_DISCONNECTEX), &bytes, NULL, NULL))
		return false;

	GUID guidAcceptEx = WSAID_ACCEPTEX;
	if (SOCKET_ERROR == WSAIoctl(mListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(GUID), &mFnAcceptEx, sizeof(LPFN_ACCEPTEX), &bytes, NULL, NULL))
		return false;


	/// RIO function table
	GUID functionTableId = WSAID_MULTIPLE_RIO;
	DWORD dwBytes = 0;

	if ( WSAIoctl(mListenSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &functionTableId, sizeof(GUID), (void**)&mRioFunctionTable, sizeof(mRioFunctionTable), &dwBytes, NULL, NULL) )
		return false;

	OVERLAPPED overlapped;
	RIO_NOTIFICATION_COMPLETION completionType;

	completionType.Type = RIO_IOCP_COMPLETION;
	completionType.Iocp.IocpHandle = mIocp;
	completionType.Iocp.CompletionKey = (void*)CK_RIO;
	completionType.Iocp.Overlapped = &overlapped;

	mRioCompletionQueue = RIO.RIOCreateCompletionQueue(MAX_CQ_SIZE, &completionType);
	if (mRioCompletionQueue == RIO_INVALID_CQ)
		CRASH_ASSERT(false);

	return true;
}


bool RIOManager::StartIoThreads()
{
	/// I/O Thread
	DWORD dwThreadId;
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, IoWorkerThread, (LPVOID)mIocp, 0, (unsigned int*)&dwThreadId);
	if (hThread == NULL)
		return false;
	
	/// notify completion-ready 
	INT notifyResult = RIO.RIONotify(mRioCompletionQueue);
	if (notifyResult != ERROR_SUCCESS)
	{
		printf_s("RIONotify Error: %d\n", GetLastError());
		CRASH_ASSERT(false);
	}

	return true;
}


bool RIOManager::StartAcceptLoop()
{
	/// listen
	if (SOCKET_ERROR == listen(mListenSocket, SOMAXCONN))
		return false;

	while (GSessionManager->AcceptSessions())
	{
		Sleep(100);
	}

	return true;
}


unsigned int WINAPI RIOManager::IoWorkerThread(LPVOID lpParam)
{
	HANDLE hIocp = lpParam;
	DWORD numberOfBytes = 0;
	ULONG_PTR completionKey = 0;
	OVERLAPPED* pOverlapped = nullptr;

	RIORESULT results[MAX_RIO_RESULT];

	while (true)
	{
		auto ret = GetQueuedCompletionStatus(hIocp, &numberOfBytes, &completionKey, &pOverlapped, INFINITE);
		if ( !ret )
		{
			printf_s("GetQueuedCompletionStatus Error: %d\n", GetLastError());
			break;
		}

		/// For AcceptEx and DisconnectEx
		if (CK_RIO != completionKey && pOverlapped != nullptr )
		{
			OverlappedContext* ovContext = (OverlappedContext*)pOverlapped;
			if ( IO_ACCEPT == ovContext->mIoType )
			{
				ovContext->mClientSession->AcceptCompletion();
				delete static_cast<OverlappedAcceptContext*>(ovContext);
			}
			else if ( IO_DISCONNECT == ovContext->mIoType)
			{
				ovContext->mClientSession->DisconnectCompletion(static_cast<OverlappedDisconnectContext*>(ovContext)->mDisconnectReason);
				delete static_cast<OverlappedDisconnectContext*>(ovContext);
			}
			else
			{
				CRASH_ASSERT(false);
			}

			continue;
		}

		/// For I/O below
		memset(results, 0, sizeof(results));
		
		ULONG numResults = RIO.RIODequeueCompletion(mRioCompletionQueue, results, MAX_RIO_RESULT);
		
		if (0 == numResults || RIO_CORRUPT_CQ == numResults)
		{
			printf_s("RIODequeueCompletion Error: %d\n", GetLastError());
			CRASH_ASSERT(false);
		}

		/// Notify after Dequeueing
		int notifyResult = RIO.RIONotify(mRioCompletionQueue);
		if (notifyResult != ERROR_SUCCESS)
		{
			printf_s("RIONotify Error: %d\n", GetLastError());
			CRASH_ASSERT(false);
		}

		for (ULONG i = 0; i < numResults; ++i)
		{
			RioIoContext* context = reinterpret_cast<RioIoContext*>(results[i].RequestContext);
			ClientSession* client = context->mClientSession;
			ULONG transferred = results[i].BytesTransferred;
		
			CRASH_ASSERT(context && client);

			if (transferred == 0)
			{
				client->DisconnectRequest(DR_ZERO_COMPLETION);
				ReleaseRioContext(context);
				continue;
			}
			
			if (IO_RECV == context->mIoType)
			{
				client->RecvCompletion(transferred);

				/// echo back
				if (false == client->PostSend())
				{
					client->DisconnectRequest(DR_IO_REQUEST_SEND_ERROR);
				}
		
			}
			else if (IO_SEND == context->mIoType)
			{
				client->SendCompletion(transferred);

				if (context->Length != transferred)
				{
					client->DisconnectRequest(DR_PARTIAL_SEND_COMPLETION);
				}
				else if (false == client->PostRecv())
				{
					client->DisconnectRequest(DR_IO_REQUEST_RECV_ERROR);
				}
			}
			else
			{
				printf_s("[DEBUG] Unknown I/O Type: %d\n", context->mIoType);
				CRASH_ASSERT(false);
			}

			ReleaseRioContext(context);
		} /// for
 	} /// while
	
	return 0;
}

void ReleaseRioContext(RioIoContext* context)
{
	/// refcount release for i/o context
	context->mClientSession->ReleaseRef();

	/// context release
	delete context;
}

