// EduServer_RIO.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Exception.h"
#include "EduServer_RIO.h"
#include "ClientSession.h"
#include "SessionManager.h"
#include "RIOManager.h"



int _tmain(int argc, _TCHAR* argv[])
{
	/// for dump on crash
	SetUnhandledExceptionFilter(ExceptionFilter);

	/// Global Managers
	GSessionManager = new SessionManager;
	GRioManager = new RIOManager;


	if (false == GRioManager->Initialize())
		return -1;

	if (false == GSessionManager->PrepareSessionPool())
		return -1;

	if (false == GRioManager->StartIoThreads())
		return -1;

	printf_s("Start Server\n");

	if (false == GRioManager->StartAcceptLoop())
		return -1;

	printf_s("End Server\n");

	delete GRioManager;
	delete GSessionManager;

	return 0;
}

