#pragma once

class ClientSession;

class RIOManager
{
public:
	RIOManager();
	~RIOManager();

	bool Initialize();

	bool StartIoThreads();
	bool StartAcceptLoop();

	const RIO_CQ& GetCompletionQueue() { return mRioCompletionQueue; }
	HANDLE GetIocp() { return mIocp; }
	SOCKET* GetListenSocket() { return &mListenSocket; }

public:
	static RIO_EXTENSION_FUNCTION_TABLE mRioFunctionTable;

	static char mAcceptBuf[64];
	static LPFN_DISCONNECTEX mFnDisconnectEx;
	static LPFN_ACCEPTEX mFnAcceptEx;

private:
	static unsigned int WINAPI IoWorkerThread(LPVOID lpParam);
	

private:
	static RIO_CQ mRioCompletionQueue;

	SOCKET	mListenSocket;
	HANDLE	mIocp;

};

void ReleaseRioContext(RioIoContext* context);

extern RIOManager* GRioManager;

BOOL DisconnectEx(SOCKET hSocket, LPOVERLAPPED lpOverlapped, DWORD dwFlags, DWORD reserved);

BOOL AcceptEx(SOCKET sListenSocket, SOCKET sAcceptSocket, PVOID lpOutputBuffer, DWORD dwReceiveDataLength,
	DWORD dwLocalAddressLength, DWORD dwRemoteAddressLength, LPDWORD lpdwBytesReceived, LPOVERLAPPED lpOverlapped);

#define RIO	RIOManager::mRioFunctionTable
