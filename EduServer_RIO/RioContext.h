#pragma once

#include "ObjectPool.h"

enum IOType
{
	IO_NONE,
	IO_SEND,
	IO_RECV,
	IO_ACCEPT,
	IO_DISCONNECT
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
	CK_RIO = 0xC0DE,

	SESSION_BUFFER_SIZE = 65536,

	MAX_RIO_RESULT = 256,
	MAX_SEND_RQ_SIZE_PER_SOCKET = 32,
	MAX_RECV_RQ_SIZE_PER_SOCKET = 4,
	MAX_CLIENT = 10000,
	MAX_CQ_SIZE = (MAX_SEND_RQ_SIZE_PER_SOCKET + MAX_RECV_RQ_SIZE_PER_SOCKET) * MAX_CLIENT,

};

class ClientSession;

struct RioIoContext : public RIO_BUF, public ObjectPool<RioIoContext>
{
	RioIoContext(ClientSession* client, IOType ioType);
	
	ClientSession* mClientSession;
	IOType	mIoType;
};

struct OverlappedContext
{
	OverlappedContext(ClientSession* client, IOType ioType);

	OVERLAPPED		mOverlapped;
	ClientSession*	mClientSession;
	IOType			mIoType;
	WSABUF			mWsaBuf;
};

struct OverlappedDisconnectContext : public OverlappedContext, public ObjectPool<OverlappedDisconnectContext>
{
	OverlappedDisconnectContext(ClientSession* owner, DisconnectReason dr)
		: OverlappedContext(owner, IO_DISCONNECT), mDisconnectReason(dr)
	{}

	DisconnectReason mDisconnectReason;
};

struct OverlappedAcceptContext : public OverlappedContext, public ObjectPool<OverlappedAcceptContext>
{
	OverlappedAcceptContext(ClientSession* owner) : OverlappedContext(owner, IO_ACCEPT)
	{}
};
