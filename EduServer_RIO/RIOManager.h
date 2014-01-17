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

	const RIO_CQ& GetCompletionQueue(int threadId) { return mRioCompletionQueue[threadId]; }

public:
	static RIO_EXTENSION_FUNCTION_TABLE mRioFunctionTable;

private:
	static unsigned int WINAPI IoWorkerThread(LPVOID lpParam);
	

private:
	static RIO_CQ mRioCompletionQueue[MAX_RIO_THREAD + 1];

	SOCKET	mListenSocket;

};

void ReleaseContext(RioIoContext* context);

extern RIOManager* GRioManager;

#define RIO	RIOManager::mRioFunctionTable
