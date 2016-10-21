#ifndef __ECHOSERVER_IOCP__H__
#define __ECHOSERVER_IOCP__H__

#define BUFSIZE 1024
#define NETWORK_PORT 5000

struct st_CLIENT
{
	OVERLAPPED SendOverlapped;
	OVERLAPPED RecvOverlapped;

	SOCKET socket;

	CAyaStreamSQ SendQ;
	CAyaStreamSQ RecvQ;

	BOOL bSendFlag;
	LONG iIOCount;
};

typedef list<st_CLIENT *> CLIENT;

void InitServer();

st_CLIENT *CreateClient();
void ReleaseClient();

void RecvPost(st_CLIENT *pClient);
void SendPost(st_CLIENT *pClient);

unsigned __stdcall AcceptThread(LPVOID acceptArg);
unsigned __stdcall SendThread(LPVOID sendArg);
unsigned __stdcall IOCPWorkerThread(LPVOID workerArg);

#endif