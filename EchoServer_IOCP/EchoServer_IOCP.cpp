#include <WinSock2.h>
#include <Windows.h>
#include <process.h>
#include <list>

#define MAX_THREAD 5

using namespace std;

#pragma comment(lib,"Ws2_32.lib")

#include "StreamQueue.h"
#include "NPacket.h"
#include "EchoServer_IOCP.h"

SOCKET listen_sock;
CLIENT g_Client;
CRITICAL_SECTION cs;
HANDLE hThread[MAX_THREAD];

void main()
{
	InitServer();

	InitializeCriticalSection(&cs);

	WaitForMultipleObjects(MAX_THREAD, hThread, TRUE, INFINITE);
}

void InitServer()
{
	HANDLE hcp;

	int retval;

	//윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return;

	//소켓
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET)	return;

	//입출력 완료 포트 생성
	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL == hcp)
		return;

	//쓰레드 생성
	unsigned int uiThreadID;
	hThread[0] = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, hcp, 0, &uiThreadID);
	if (hThread == NULL)
		return;
	CloseHandle(hThread);

	hThread[1] = (HANDLE)_beginthreadex(NULL, 0, SendThread, hcp, 0, &uiThreadID);
	if (hThread == NULL)
		return;
	CloseHandle(hThread);

	for (int iCnt = 2; iCnt < 5; iCnt++)
	{
		hThread[iCnt] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, hcp, 0, &uiThreadID);
		if (hThread == NULL)
			return;
		CloseHandle(hThread);
	}

	//bind
	SOCKADDR_IN serveraddr;
	memset(&serveraddr, 0, sizeof(SOCKADDR_IN));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(NETWORK_PORT);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

	retval = bind(listen_sock, (SOCKADDR *)&serveraddr, sizeof(SOCKADDR_IN));
	if (retval == SOCKET_ERROR)
		return;

	//listen
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		return;
}

unsigned __stdcall AcceptThread(LPVOID acceptArg)
{
	HANDLE hcp = (HANDLE)acceptArg;
	SOCKADDR_IN sockaddr;
	int addrlen = sizeof(SOCKADDR_IN);

	int retval;

	st_CLIENT *pClient = CreateClient();
	pClient->socket = accept(listen_sock, (SOCKADDR *)&sockaddr, &addrlen);
	if (pClient->socket == INVALID_SOCKET)
		return 1;

	g_Client.push_back(pClient);
	
	hcp = CreateIoCompletionPort((HANDLE)pClient->socket, hcp, (DWORD)pClient, 0);

	RecvPost(pClient);
}

/*
컨텐츠에서 Send 작업시 -> SendQ 에 넣음 -> SendThread 에서

모든 접속 클라이언트에 대해 SendQ 확인.

SendQ 데이터가 있고, SendFlag 가 false 인 클라이언트에 대해 Send 작업

SendQ 에서 메시지를 뽑아서 -> WSABUFF 인스턴스 선언 후 SendBuff 지정

SendFlag = true -> WSASend 호출
*/
unsigned __stdcall SendThread(LPVOID sendArg)
{
	CLIENT::iterator cIter;

	while (1)
	{
		for (cIter = g_Client.begin(); cIter != g_Client.end(); ++cIter)
		{
			st_CLIENT *pClient = (*cIter);

			if (pClient->SendQ.GetUseSize() > 0 && pClient->bSendFlag == FALSE)
			{
				WSABUF sendBuf;
				sendBuf.buf = pClient->SendQ.GetReadBufferPtr();
				sendBuf.len = pClient->SendQ.GetNotBrokenGetSize();
				pClient->bSendFlag = TRUE;
				WSASend(pClient->socket, &sendBuf, 1, NULL, 0, &pClient->SendOverlapped, NULL);
			}
		}

		Sleep(20);
	}
}

/*
WSABUF 에 RecvBuf 지정 -> WSARecv 호출.

WSARecv 완료 -> RecvQ 에서 완료된 패킷에 대한 로직 처리.
*/
unsigned __stdcall IOCPWorkerThread(LPVOID workerArg)
{
	DWORD dwTransferred = 0;

	HANDLE hcp = (HANDLE)workerArg;
	WSABUF RecvBuf;
	OVERLAPPED *pOverlapped = NULL;
	st_CLIENT *pClient = NULL;

	int retval;

	while (1)
	{
		retval = GetQueuedCompletionStatus(hcp, &dwTransferred, (LPDWORD)pClient,
			(LPOVERLAPPED *)pOverlapped, INFINITE);

		if (0 == dwTransferred)
		{
			shutdown(pClient->socket, SD_BOTH);
		}

		//recv 처리
		else if (pOverlapped == &pClient->RecvOverlapped)
		{
			SendPost(pClient);
		}

		//send 처리
		else if (pOverlapped == &pClient->SendOverlapped)
		{

		}

		if (0 == InterlockedDecrement(&pClient->iIOCount))
			ReleaseClient();
	}
}

st_CLIENT *CreateClient()
{
	st_CLIENT *pClient = new st_CLIENT;

	memset(&pClient->SendOverlapped, 0, sizeof(OVERLAPPED));
	memset(&pClient->RecvOverlapped, 0, sizeof(OVERLAPPED));

	pClient->socket = INVALID_SOCKET;

	pClient->RecvQ.ClearBuffer();
	pClient->SendQ.ClearBuffer();

	pClient->bSendFlag = FALSE;
	pClient->iIOCount = 0;

	return pClient;
}

void ReleaseClient()
{

}

void RecvPost(st_CLIENT *pClient)
{
	int retval, iCount;
	WSABUF wBuf;

	wBuf.buf = pClient->RecvQ.GetWriteBufferPtr();
	wBuf.len = pClient->RecvQ.GetNotBrokenPutSize();

	InterlockedIncrement(&pClient->iIOCount);
	retval = WSARecv(pClient->socket, &wBuf, 1, NULL, 0, &pClient->RecvOverlapped, NULL);
	if (retval == SOCKET_ERROR)
	{
		if (GetLastError() != WSA_IO_PENDING)
		{
			if (0 == InterlockedDecrement(&pClient->iIOCount))
				ReleaseClient();
			return;
		}
	}
}

void SendPost(st_CLIENT *pClient)
{
	int retval, iCount;
	WSABUF wBuf;

	wBuf.buf = pClient->SendQ.GetWriteBufferPtr();
	wBuf.len = pClient->SendQ.GetNotBrokenGetSize();

	InterlockedIncrement(&pClient->iIOCount);
	retval = WSASend(pClient->socket, &wBuf, 1, NULL, 0, &pClient->SendOverlapped, NULL);
	if (retval == SOCKET_ERROR)
	{
		if (GetLastError() != WSA_IO_PENDING)
		{
			if (0 == InterlockedDecrement(&pClient->iIOCount))
				ReleaseClient();
			return;
		}
	}
}