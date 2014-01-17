#pragma once

#include "ObjectPool.h"

enum IOType
{
	IO_NONE,
	IO_SEND,
	IO_RECV,
};

enum DisconnectReason
{
	DR_NONE,
	DR_ACTIVE,
	DR_ONCONNECT_ERROR,
	DR_ZERO_COMPLETION,
	DR_IO_REQUEST_SEND_ERROR,
	DR_IO_REQUEST_RECV_ERROR,
	DR_PARTIAL_SEND_COMPLETION,
};

enum RioConfig
{
	SESSION_BUFFER_SIZE = 65536,

	MAX_RIO_THREAD = 4,
	MAX_RIO_RESULT = 256,
	MAX_SEND_RQ_SIZE_PER_SOCKET = 32,
	MAX_RECV_RQ_SIZE_PER_SOCKET = 32,
	MAX_CLIENT_PER_RIO_THREAD = 2560,
	MAX_CQ_SIZE_PER_RIO_THREAD = (MAX_SEND_RQ_SIZE_PER_SOCKET + MAX_RECV_RQ_SIZE_PER_SOCKET) * MAX_CLIENT_PER_RIO_THREAD,

};

class ClientSession;

struct RioIoContext : public RIO_BUF, public ObjectPool<RioIoContext>
{
	RioIoContext(ClientSession* client, IOType ioType);
	
	ClientSession* mClientSession;
	IOType	mIoType;
};


