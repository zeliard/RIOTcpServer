#include "stdafx.h"
#include "RioContext.h"
#include "ClientSession.h"


RioIoContext::RioIoContext(ClientSession* client, IOType ioType)
	: mClientSession(client), mIoType(ioType)
{
	mClientSession->AddRef();
}