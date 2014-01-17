#include "stdafx.h"
#include "EduServer_RIO.h"
#include "RioContext.h"
#include "RIOManager.h"
#include "ClientSession.h"
#include "SessionManager.h"


RIO_EXTENSION_FUNCTION_TABLE RIOManager::mRioFunctionTable = { 0, };
RIO_CQ RIOManager::mRioCompletionQueue[MAX_RIO_THREAD+1] = { 0, };

RIOManager* GRioManager = nullptr;

RIOManager::RIOManager() : mListenSocket(NULL)
{
}


RIOManager::~RIOManager()
{
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
	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO);
	if (mListenSocket == INVALID_SOCKET)
		return false;

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

	/// RIO function table
	GUID functionTableId = WSAID_MULTIPLE_RIO;
	DWORD dwBytes = 0;

	if ( WSAIoctl(mListenSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &functionTableId, sizeof(GUID), (void**)&mRioFunctionTable, sizeof(mRioFunctionTable), &dwBytes, NULL, NULL) )
		return false;

	return true;
}


bool RIOManager::StartIoThreads()
{
	/// I/O Thread
	for (int i = 0; i < MAX_RIO_THREAD; ++i)
	{
		DWORD dwThreadId;
		HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, IoWorkerThread, (LPVOID)(i + 1), 0, (unsigned int*)&dwThreadId);
		if (hThread == NULL)
			return false;
	}

	return true;
}


bool RIOManager::StartAcceptLoop()
{
	/// listen
	if (SOCKET_ERROR == listen(mListenSocket, SOMAXCONN))
		return false;


	/// accept loop
	while (true)
	{
		SOCKET acceptedSock = accept(mListenSocket, NULL, NULL);
		if (acceptedSock == INVALID_SOCKET)
		{
			printf_s("[DEBUG] accept: invalid socket\n");
			continue;
		}

		SOCKADDR_IN clientaddr;
		int addrlen = sizeof(clientaddr);
		getpeername(acceptedSock, (SOCKADDR*)&clientaddr, &addrlen);

		/// new client session (should not be under any session locks)
		ClientSession* client = GSessionManager->IssueClientSession();

		/// connection establishing and then issuing recv
		if (false == client->OnConnect(acceptedSock, &clientaddr))
		{
			client->Disconnect(DR_ONCONNECT_ERROR);
		}
	}

	return true;
}


unsigned int WINAPI RIOManager::IoWorkerThread(LPVOID lpParam)
{
	LIoThreadId = reinterpret_cast<int>(lpParam);
	

	mRioCompletionQueue[LIoThreadId] = RIO.RIOCreateCompletionQueue(MAX_CQ_SIZE_PER_RIO_THREAD, 0);
	if (mRioCompletionQueue[LIoThreadId] == RIO_INVALID_CQ)
	{
		CRASH_ASSERT(false);
		return -1;
	}
		

	RIORESULT results[MAX_RIO_RESULT];

	while (true)
	{
		memset(results, 0, sizeof(results));
		
		ULONG numResults = RIO.RIODequeueCompletion(mRioCompletionQueue[LIoThreadId], results, MAX_RIO_RESULT);
		
		if (0 == numResults)
		{
			Sleep(1); ///< for low cpu-usage
			continue;
		}
		else if (RIO_CORRUPT_CQ == numResults)
		{
			printf_s("[DEBUG] RIO CORRUPT CQ \n");
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
				CRASH_ASSERT(client->GetRioThreadId() == LIoThreadId);

				client->Disconnect(DR_ZERO_COMPLETION);
				ReleaseContext(context);
				continue;
			}
			else if (IO_RECV == context->mIoType)
			{
				client->RecvCompletion(transferred);

				/// echo back
				if (false == client->PostSend())
				{
					client->Disconnect(DR_IO_REQUEST_SEND_ERROR);
				}
		
			}
			else if (IO_SEND == context->mIoType)
			{
				client->SendCompletion(transferred);

				if (context->Length != transferred)
				{
					client->Disconnect(DR_PARTIAL_SEND_COMPLETION);
				}
				else if (false == client->PostRecv())
				{
					client->Disconnect(DR_IO_REQUEST_RECV_ERROR);
				}
			}
			else
			{
				printf_s("[DEBUG] Unknown I/O Type: %d\n", context->mIoType);
				CRASH_ASSERT(false);
			}

			ReleaseContext(context);
		
		} /// for

	
 	} /// while

	
	return 0;
}

void ReleaseContext(RioIoContext* context)
{
	/// refcount release for i/o context
	context->mClientSession->ReleaseRef();

	/// context release
	delete context;
}
