#include "stdafx.h"
#include "RioContext.h"
#include "ClientSession.h"


RioIoContext::RioIoContext(ClientSession* client, IOType ioType)
	: mClientSession(client), mIoType(ioType)
{
	mClientSession->AddRef();
}

OverlappedContext::OverlappedContext(ClientSession* client, IOType ioType)
	: mClientSession(client), mIoType(ioType)
{
	memset(&mOverlapped, 0, sizeof(OVERLAPPED));
	memset(&mWsaBuf, 0, sizeof(WSABUF));
	mClientSession->AddRef();
}
