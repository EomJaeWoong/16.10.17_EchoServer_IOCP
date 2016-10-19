#include <WinSock2.h>
#include <process.h>
#include <list>

using namespace std;

#include "StreamQueue.h"
#include "NPacket.h"
#include "EchoServer_IOCP.h"

SOCKET listen_sock;
CLIENT g_Client;
HANDLE hcp;

void main()
{
	InitServer();
}

void InitServer()
{
	int retval;

	//���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return;

	//����
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET)	return;

	//����� �Ϸ� ��Ʈ ����
	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL == hcp)
		return;

	//������ ����
	HANDLE hThread;
	unsigned int uiThreadID;
	hThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, NULL, 0, &uiThreadID);
	if (hThread == NULL)
		return;
	CloseHandle(hThread);

	hThread = (HANDLE)_beginthreadex(NULL, 0, SendThread, NULL, 0, &uiThreadID);
	if (hThread == NULL)
		return;
	CloseHandle(hThread);

	for (int iCnt = 0; iCnt < 3; iCnt++)
	{
		hThread = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, NULL, 0, &uiThreadID);
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
	WSABUF wBuf;
	SOCKADDR_IN sockaddr;
	int addrlen = sizeof(SOCKADDR_IN);

	int retval;

	st_CLIENT *pClient = CreateClient();
	pClient->socket = accept(listen_sock, (SOCKADDR *)&sockaddr, &addrlen);
	if (pClient->socket == INVALID_SOCKET)
		return;

	g_Client.push_back(pClient);

	InterlockedIncrement(&pClient->iIOCount);
	retval = WSARecv(pClient->socket, &wBuf, 1, NULL, 0, &pClient->RecvOverlapped, NULL);
	if (retval == SOCKET_ERROR)
	{
		if (GetLastError() != WSA_IO_PENDING)
		{
			InterlockedDecrement(&pClient->iIOCount);
			return;
		}
	}
}

/*
���������� Send �۾��� -> SendQ �� ���� -> SendThread ����

��� ���� Ŭ���̾�Ʈ�� ���� SendQ Ȯ��.

SendQ �����Ͱ� �ְ�, SendFlag �� false �� Ŭ���̾�Ʈ�� ���� Send �۾�

SendQ ���� �޽����� �̾Ƽ� -> WSABUFF �ν��Ͻ� ���� �� SendBuff ����

SendFlag = true -> WSASend ȣ��
*/
unsigned __stdcall SendThread(LPVOID sendArg)
{
	CLIENT::iterator cIter;

	for (cIter = g_Client.begin(); cIter != g_Client.end(); ++cIter)
	{
		st_CLIENT *pClient = (*cIter);

		if (pClient->SendQ.GetUseSize() > 0 && pClient->bSendFlag == FALSE)
		{
			WSABUF sendBuf;
			sendBuf.buf = pClient->SendQ.GetReadBufferPtr();

			pClient->bSendFlag = TRUE;
			WSASend(pClient->socket, &sendBuf, 1, NULL, 0, &pClient->SendOverlapped, NULL);
		}
	}

	Sleep(20);
}

/*
WSABUF �� RecvBuf ���� -> WSARecv ȣ��.

WSARecv �Ϸ� -> RecvQ ���� �Ϸ�� ��Ŷ�� ���� ���� ó��.
*/
unsigned __stdcall IOCPWorkerThread(LPVOID workerArg)
{
	DWORD dwTransferred;

	WSABUF RecvBuf;
	st_CLIENT stClient;

	GetQueuedCompletionStatus((HANDLE)workerArg, &dwTransferred, (LPDWORD)&stClient.socket,
		(LPOVERLAPPED *)&stClient.RecvOverlapped, INFINITE);

	if (0 == dwTransferred)
	{
		shutdown(stClient.socket, SD_BOTH);
	}

	else if ()
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