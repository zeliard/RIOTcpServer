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
	HANDLE GetIocp() { return mIocp;  }

public:
	static RIO_EXTENSION_FUNCTION_TABLE mRioFunctionTable;

private:
	static unsigned int WINAPI IoWorkerThread(LPVOID lpParam);
	

private:
	static RIO_CQ mRioCompletionQueue;

	SOCKET	mListenSocket;
	HANDLE	mIocp;

};

void ReleaseContext(RioIoContext* context);

extern RIOManager* GRioManager;

#define RIO	RIOManager::mRioFunctionTable
