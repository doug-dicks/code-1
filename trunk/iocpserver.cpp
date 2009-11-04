//  ���� , TAB = 8�ո�
#include "stdafx.h"
#include "iocpserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <process.h>

/* �ڲ�����  -------------------------------------------------------------------------------------------------*/
#define MAX_UNSENDS_COUNT        100	// Maximun send list.
#define DEFAULT_ACCEPTEX_COUNT   100	// Maximum number of acceptex count

#define SPIN_COUNT_SOCK		64	//����sockobj��spin count
#define SPIN_COUNT_ALIVE	64	//����Alive�б��spin count
#define SPIN_COUNT_CLOSE	64	//����Close�б��spin count	
/*-------------------------------------------------------------------------------------------------------------*/

#define OP_NONE         -1
#define OP_ACCEPT       0		// AcceptEx
#define OP_READ		1		// WSARecv
#define OP_WRITE        2		// WSASend

//variables for log server status
#ifdef LOG_STATUS
volatile long gTotalSock = 0;  
volatile long gTotalBuf = 0;  
volatile long gIoCount = 0;
#endif 

#define LOCK EnterCriticalSection
#define UNLOCK LeaveCriticalSection

#ifdef LOG_ENABLE
CLog Log_Server;
#endif
//---------------------------------------------------------------------------------------------------------------------------
//ResolveAddress()
//Resolve address by params
//---------------------------------------------------------------------------------------------------------------------------
struct addrinfo *CIocpServer::ResolveAddress(const char *addr,const char *port, int af, int type, int proto)
{
	struct addrinfo hints,
		*res = NULL;
	int             rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags  = ((addr) ? 0 : AI_PASSIVE);
	hints.ai_family = af;
	hints.ai_socktype = type;
	hints.ai_protocol = proto;

	rc = getaddrinfo(
		addr,
		port,
		&hints,
		&res
		);
	if (rc != 0)	{
		fprintf(stderr,"Invalid address %s, getaddrinfo failed: %d\n", addr, rc);
		return NULL;
	}
	return res;
}
//---------------------------------------------------------------------------------------------------------------------------
//Constructor
//---------------------------------------------------------------------------------------------------------------------------
CIocpServer::CIocpServer()
:m_Threads(0),m_CLThreads(0),m_bAdo(false),m_UnlimitPort(UNLIMIT_PORT_START)
{
	//��ʼ���ڴ�ض���
	m_sockobj = new cFixedMemoryPool(sizeof(SOCKET_OBJ),(int)(AVERAGE_CONNECTIONS*2),1);
	m_bufobj = new cFixedMemoryPool(sizeof(BUFFER_OBJ),(int)(AVERAGE_CONNECTIONS*3));
	m_buffer  =  new cFixedMemoryPool(DEFAULT_BUFFER_SIZE,(int)(AVERAGE_CONNECTIONS*3));

	InitializeCriticalSectionAndSpinCount(&secAlive,SPIN_COUNT_ALIVE);
	InitializeCriticalSectionAndSpinCount(&secClose,SPIN_COUNT_CLOSE);

	m_Inited = false;
	CompletionPort = INVALID_HANDLE_VALUE;
	CLCompletionPort = INVALID_HANDLE_VALUE;
	m_accept_count = 0;

#ifdef LOG_ENABLE	//���ݿ�ִ���ļ���,�Զ�����һ����־�ļ�.
	char log_name[512];
	char *pname;
	GetModuleFileName(NULL,log_name,sizeof(log_name));
	pname = log_name+strlen(log_name) -1;
	*pname = 't';pname--;
	*pname = 'x';pname--;
	*pname = 't';
	if(Log_Server.Init(log_name))
		printf("����Log:\"%s\"   ",log_name);
#endif

}

//---------------------------------------------------------------------------------------------------------------------------
//Destructor
//---------------------------------------------------------------------------------------------------------------------------
CIocpServer::~CIocpServer()
{
	if(m_Inited)
	{
		WSACleanup();
		Sleep(500);

		printf("���ڹر�...");

		for(int i=1;i<5;i++)
		{
			printf(" %d",i);
			Sleep(1000);
		}			

		CloseHandle(CompletionPort);
		CloseHandle(CLCompletionPort);
	}

	DeleteCriticalSection(&secAlive);
	DeleteCriticalSection(&secClose);

	delete m_sockobj;
	delete m_bufobj; 
	delete m_buffer;
}
//---------------------------------------------------------------------------------------------------------------------------
//GetBufferObj()
//Alloc buffer objects
//---------------------------------------------------------------------------------------------------------------------------
BUFFER_OBJ *CIocpServer::GetBufferObj()
{
	BUFFER_OBJ *newobj=NULL;
	char *pbuf;

#ifdef LOG_STATUS
	InterlockedIncrement(&gTotalBuf);
#endif

	//����BUFFER_OBJ����
	newobj = (BUFFER_OBJ *)m_bufobj->Alloc();
	//����BUFFER_OBJ�е�buf����
	pbuf = (char*)m_buffer->Alloc();

	//��ʼ��
	ZeroMemory(newobj,sizeof(BUFFER_OBJ));
	//ZeroMemory(pbuf,DEFAULT_BUFFER_SIZE);  //�������ݿ��Բ��س�ʼ��
	newobj->sclient = INVALID_SOCKET;
	newobj->buf = pbuf;

	return newobj;
}
//---------------------------------------------------------------------------------------------------------------------------
//FreeBufferObj()
//Delete buffer objects
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::FreeBufferObj(BUFFER_OBJ *obj)
{
	m_buffer->Free(obj->buf);
	m_bufobj->Free(obj);

#ifdef LOG_STATUS
	InterlockedDecrement(&gTotalBuf);
#endif
}
//---------------------------------------------------------------------------------------------------------------------------
//GetSocketObj()
//Alloc socket objects
//---------------------------------------------------------------------------------------------------------------------------
SOCKET_OBJ * CIocpServer::GetSocketObj(SOCKET s) //get
{
	SOCKET_OBJ  *sockobj=NULL;

#ifdef LOG_STATUS
	InterlockedIncrement(&gTotalSock);
#endif

	sockobj = (SOCKET_OBJ *)m_sockobj->Alloc();

	ZeroMemory(sockobj,sizeof(SOCKET_OBJ));
	sockobj->s = s;

#ifdef DEBUG_IOCP	//�ж����sock�Ƿ��Ѿ����ͷ���
	sockobj->freeed = 1;
	sockobj->onclosed = 1;
#endif

	InitializeCriticalSectionAndSpinCount(&sockobj->cs,SPIN_COUNT_SOCK);

	return sockobj;
}
//---------------------------------------------------------------------------------------------------------------------------
//FreeSocketObj()
//Delete socket objects
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::FreeSocketObj(SOCKET_OBJ *obj)
{	
	_ASSERTE(obj->s == INVALID_SOCKET && obj->sendobj == NULL && obj->recvobj == NULL);
#ifdef DEBUG_IOCP
	obj->freeed = 1;
#endif

	while(obj->sending_count != 0) //��ȫfree sockobj
		Sleep(1);

	DeleteCriticalSection(&obj->cs);	

	m_sockobj->Free(obj);

#ifdef LOG_STATUS
	InterlockedDecrement(&gTotalSock);
#endif
}

//---------------------------------------------------------------------------------------------------------------------------
//AddAlive()
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::AddAlive(SOCKET_OBJ *sock,DWORD time)
{
	//��ӵ������б�
	LOCK(&secAlive);

	sock->flag_alive = 1;
	sock->timealive = time;
	m_mapAlive[sock] = time;

	UNLOCK(&secAlive);
}

//---------------------------------------------------------------------------------------------------------------------------
//DeleteAlive()
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::DeleteAlive(SOCKET_OBJ *sock)
{
	//���б���ɾ���������
	map<SOCKET_OBJ *,DWORD>::iterator it;

	LOCK(&secAlive);

	if(sock->flag_alive == 1)
	{
		it = m_mapAlive.find(sock);
		if(it != m_mapAlive.end())
		{
			it->first->flag = 0;
			m_mapAlive.erase(it);
		}
	}

	UNLOCK(&secAlive);
}

//---------------------------------------------------------------------------------------------------------------------------
//CheckAlive()
//Check for keepalive timeout
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::CheckAlive()
{
	//��鳬ʱ
	map<SOCKET_OBJ *,DWORD>::iterator it,ittmp;
	SOCKET s;

	DWORD time = GetTickCount();

	LOCK(&secAlive);

	it = m_mapAlive.begin();
	while(it != m_mapAlive.end())
	{
		ittmp = it;
		++it;

		if(time - ittmp->first->timealive > KEEPALIVE_TIME)	
		{			
			s = ittmp->first->s;				//�Ȱ�SOCKOBJ��s��ΪINVALID_SOCKET
			ittmp->first->s = INVALID_SOCKET;	//�ٹرվ��,Ϊ���̰߳�ȫ
			ittmp->first->flag_alive = 0;
			closesocket(s);
			m_mapAlive.erase(ittmp);
		}
	}

	UNLOCK(&secAlive);
}

//---------------------------------------------------------------------------------------------------------------------------
//AddClose()
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::AddClose(SOCKET_OBJ *sock,DWORD time)
{
	//��ӵ���ʱ�ر��б�
	LOCK(&secClose);

	sock->flag_close = 1;
	m_mapClose[sock] = time;

	UNLOCK(&secClose);
}
//---------------------------------------------------------------------------------------------------------------------------
//DeleteClose()
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::DeleteClose(SOCKET_OBJ *sock)
{
	//���б���ɾ��
	map<SOCKET_OBJ *,DWORD>::iterator it;

	LOCK(&secClose);

	if(sock->flag_close == 1)
	{
		it = m_mapClose.find(sock);
		if(it != m_mapClose.end()) 
		{
			it->first->flag_close = 0;
			m_mapClose.erase(it);
		}
	}

	UNLOCK(&secClose);
}

//---------------------------------------------------------------------------------------------------------------------------
//CheckClose()
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::CheckClose()
{
	//�����ʱ�رճ�ʱ
	map<SOCKET_OBJ *,DWORD>::iterator it,ittmp;
	SOCKET s;

	DWORD time = GetTickCount();

	LOCK(&secClose);

	it = m_mapClose.begin();
	while(it != m_mapClose.end()) 
	{
		ittmp = it;
		++it;
		if(time - ittmp->second > CLOSE_DELAY) 
		{	
			ittmp->first->flag_delay_close = 0;
			s = ittmp->first->s;			 //�Ȱ�SOCKOBJ��s��ΪINVALID_SOCKET
			ittmp->first->s = INVALID_SOCKET;//Ȼ����ùرտ��Ա�֤�̰߳�ȫ
			closesocket(s);
			m_mapClose.erase(ittmp);
		}
	}

	UNLOCK(&secClose);
}
//---------------------------------------------------------------------------------------------------------------------------
//PostRecv()
//return -1 when error , else return 0
//---------------------------------------------------------------------------------------------------------------------------
int CIocpServer::PostRecv(SOCKET_OBJ *sock, BUFFER_OBJ *recvobj)
{
	WSABUF  wbuf;
	DWORD   bytes,
		flags;
	int     rc;

	if(recvobj->buflen >= DEFAULT_BUFFER_SIZE)
	{ 
#ifdef LOG_LEVEL2
		Log_Server.Write("(->%d)���ջ������",sock->s);
#endif
		CloseSock(sock);
		return -1;
	}

	recvobj->operation = OP_READ;

	wbuf.buf = recvobj->buf+recvobj->buflen;			//�����ջ��嶨λ��ԭ�����ݵ�ĩβ
	wbuf.len = DEFAULT_BUFFER_SIZE - recvobj->buflen;	//�������ÿɽ������ݵĳ���

	flags = 0;

	EnterCriticalSection(&sock->cs);

	rc = WSARecv(
		sock->s,
		&wbuf,
		1,
		&bytes,
		&flags,
		&recvobj->ol,
		NULL
		);

	if (rc == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING) 
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("(->%d)PostRecv����: %d",sock->s,WSAGetLastError());
#endif           
			LeaveCriticalSection(&sock->cs);
			return SOCKET_ERROR;
		}
	}

	LeaveCriticalSection(&sock->cs);

#ifdef LOG_STATUS
	InterlockedIncrement(&gIoCount);
#endif

	return NO_ERROR;
}

//---------------------------------------------------------------------------------------------------------------------------
//PostSend()
//return -1 when error, 0 - ok
//---------------------------------------------------------------------------------------------------------------------------
int CIocpServer::PostSend(SOCKET_OBJ *sock, BUFFER_OBJ *sendobj)
{
	WSABUF  wbuf;
	DWORD   bytes;
	int     rc;

	EnterCriticalSection(&sock->cs);

	sendobj->operation = OP_WRITE;
	wbuf.buf = sendobj->buf;
	wbuf.len = sendobj->buflen;
	rc = WSASend(
		sock->s,
		&wbuf,
		1,
		&bytes,
		0,
		&sendobj->ol,
		NULL
		);

#ifdef DEBUG_IOCP
	sock->lastsend = sendobj;
#endif
	sendobj->buflen = DEFAULT_BUFFER_SIZE;	//����Ϊ���ֵ����õ���Send()ʱ������׷�ӵ����bufferobj
											//Send�����������ж�ԭ�ж�����buffer���ݵ����ݣ����С��DEFAULT_BUFFER_SIZE
											//���͵����ݽ���ӵ�ǰһ��buffer obj����

	if (rc == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING) 
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("(->%d)PostSend����: %d",sock->s,WSAGetLastError());
#endif    
			LeaveCriticalSection(&sock->cs);
			return SOCKET_ERROR;
		}
	}

	LeaveCriticalSection(&sock->cs);

#ifdef LOG_STATUS
	InterlockedIncrement(&gIoCount);
#endif
	return NO_ERROR;
}

//---------------------------------------------------------------------------------------------------------------------------
//PostAccept()
//---------------------------------------------------------------------------------------------------------------------------
int CIocpServer::PostAccept(SOCKET_OBJ *sock, BUFFER_OBJ *acceptobj)
{
	DWORD   bytes;
	int     rc;

	acceptobj->operation = OP_ACCEPT;

	LOCK(&sock->cs);

	acceptobj->sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (acceptobj->sclient == INVALID_SOCKET) 
	{  
#ifdef LOG_LEVEL2
		Log_Server.Write("PostAccept(socket())����: %d",WSAGetLastError());
#endif 			rc = -1;
	}
	else
	{
		rc = lpfnAcceptEx(
			sock->s,
			acceptobj->sclient,
			acceptobj->buf,
			0,				//acceptobj->buflen - ((sizeof(SOCKADDR_STORAGE) + 16) * 2),
			//�ѻ����С����Ϊ0������ʹAccept���յ����Ӿ��������ء�
			sizeof(SOCKADDR_STORAGE) + 16,
			sizeof(SOCKADDR_STORAGE) + 16,
			&bytes,
			&acceptobj->ol
			);
		if (rc == FALSE && WSAGetLastError() != WSA_IO_PENDING)
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("(->%d)PostAccept����: %d",sock->s,WSAGetLastError());
#endif    
			closesocket(acceptobj->sclient);
			acceptobj->sclient = INVALID_SOCKET;
		} 
		else 
		{
#ifdef LOG_STATUS
			InterlockedIncrement(&gIoCount);
#endif
			InterlockedIncrement(&m_accept_count);
			rc = 0;
		}
	}

	UNLOCK(&sock->cs);

	return rc;
}

//---------------------------------------------------------------------------------------------------------------------------
//InitClient()
//-1 - error , 0 - ok
//threadnum - number of thread to create for connect
//---------------------------------------------------------------------------------------------------------------------------
int CIocpServer::InitClient(int threadnum)
{
	HANDLE rh;

	//�����������ӵ���ɶ˿�
	CLCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
	if (CLCompletionPort == NULL) 
	{
		fprintf(stderr, "CLCreateIoCompletionPort failed: %d\n", GetLastError());
		return -1;
	}

	for(int i=0; i < threadnum;i++)
	{
		rh = (HANDLE)_beginthreadex(NULL, 0, CLCompletionThread,this, 0, NULL);
		if (rh == NULL)
		{
			fprintf(stderr, "CLCreatThread failed: %d\n", GetLastError());
			return -1;
		}

		CloseHandle(rh);
	}

	return 0;
}

//---------------------------------------------------------------------------------------------------------------------------
//InitServer()
//-1 - error , 0 - ok
//threadnum - number of thread to create for listen
//---------------------------------------------------------------------------------------------------------------------------
int CIocpServer::InitServer(int threadnum)
{
	//�������ڼ�������ɶ˿ھ��
	CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
	if (CompletionPort == NULL) 
	{
		fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
		return -1;
	}

	//���������߳�
	for(int i=0; i < threadnum;i++) 
	{
		HANDLE CompThreads = (HANDLE)_beginthreadex(NULL, 0, CompletionThread,this, 0, NULL);
		if (CompThreads == NULL) 
		{
			fprintf(stderr, "CreatThread failed: %d\n", GetLastError());
			return -1;
		}

		CloseHandle(CompThreads);
	}

	return 0;
}
//---------------------------------------------------------------------------------------------------------------------------
//Init()
//-1 - error , 0 - ok
//ConnectThread -- thread number for connect
//ListenThread -- thread number for listen
//bAdoConnect -- use ado class?
//szConnect -- ado connection string
//---------------------------------------------------------------------------------------------------------------------------
int CIocpServer::Init(int connect_thread,int listen_thread,int adoflag,char *adostring)
{
	int irc=0;
	if(m_Inited)
	{
		fprintf(stderr, "��ʼ����һ����!\n");
		return -1;
	}

	WSADATA          wsd;
	// Load Winsock
	if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) 
	{
		fprintf(stderr, "unable to load Winsock!\n");
		return -1;
	}

	m_Inited = true;
	m_CLThreads = connect_thread;
	m_Threads = listen_thread;
	m_bAdo = adoflag;
	if(adostring!=NULL)
		m_szAdo = adostring;

	if(m_CLThreads > 0)
	{
		irc = InitClient(m_CLThreads);
		if(irc != 0)
			return irc;
	}

	if(m_Threads > 0)
		irc = InitServer(m_Threads);


	return irc;
}

//---------------------------------------------------------------------------------------------------------------------------
//StartServer()
//start  listen
//---------------------------------------------------------------------------------------------------------------------------
int  CIocpServer::StartServer(const char *local_ip,const char *local_port)
{
	if(!m_Inited)
	{
		fprintf(stderr, "Run Init() first !\n");
		return -1;
	}

	SOCKET_OBJ      *sockobj=NULL;
	HANDLE           hrc;
	int              endpointcount=0,
					 rc,
					 i;
	HANDLE			 event_accept; 

	struct addrinfo *res=NULL,
		*ptr=NULL;


#ifdef LOG_LEVEL2
	printf("LOG_LEVEL2");
#else 
//
#ifdef LOG_LEVEL1
	printf("LOG_LEVEL1");
#else
	printf("LOG_NONE");
#endif
//
#endif

	printf("\n\n�����߳�:%d: �����߳�:%d; ƽ��������:%d ; ",
		m_CLThreads,m_Threads,AVERAGE_CONNECTIONS);
	printf("IOCP�汾: %s;\n",IOCP_VERSION);

#ifdef ENABLE_KEEPALIVE
	printf("�������:����; ");
#else
	printf("�������:�ر�; ");
#endif
	printf("������ʱ:%d; �ر���ʱ:%d\n",KEEPALIVE_TIME,CLOSE_DELAY);


	res = ResolveAddress(local_ip, local_port, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (res == NULL)
	{
		fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
		return -1;
	}

	ptr = res;
	if (ptr)
	{
		sockobj = GetSocketObj(INVALID_SOCKET);
		// create the socket
		sockobj->s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (sockobj->s == INVALID_SOCKET)
		{
			fprintf(stderr,"socket failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Associate the socket and its SOCKET_OBJ to the completion port
		hrc = CreateIoCompletionPort((HANDLE)sockobj->s, CompletionPort, (ULONG_PTR)sockobj, 0);
		if (hrc == NULL)
		{
			fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
			return -1;
		}

		// bind the socket to a local address and port
		rc = bind(sockobj->s, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
			return -1;
		}

		BUFFER_OBJ *acceptobj=NULL;
		GUID        guidAcceptEx = WSAID_ACCEPTEX;
		GUID	    guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
		DWORD       bytes;

		// Need to load the Winsock extension functions from each provider
		//    -- e.g. AF_INET and AF_INET6. 
		rc = WSAIoctl(
			sockobj->s,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guidAcceptEx,
			sizeof(guidAcceptEx),
			&lpfnAcceptEx,
			sizeof(lpfnAcceptEx),
			&bytes,
			NULL,
			NULL
			);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "WSAIoctl: SIO_GET_EXTENSION_FUNCTION_POINTER failed: %d\n",
				WSAGetLastError());
			return -1;
		}

		rc = WSAIoctl(
			sockobj->s,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guidGetAcceptExSockaddrs,
			sizeof(guidGetAcceptExSockaddrs),
			&lpfnGetAcceptExSockaddrs,
			sizeof(lpfnGetAcceptExSockaddrs),
			&bytes,
			NULL,
			NULL
			);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "WSAIoctl: SIO_GET_EXTENSION_FUNCTION_POINTER faled: %d\n",
				WSAGetLastError());
			return -1;
		}

		// For TCP sockets, we need to "listen" on them
		rc = _(sockobj->s, 32);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
			return -1;
		}
		
		event_accept =  CreateEvent(NULL, TRUE, FALSE, NULL);
		if(event_accept == NULL)
		{
			fprintf(stderr,"event_accept CreateEvent failed: %d\n",GetLastError());
			return -1;
		}

		rc = WSAEventSelect(sockobj->s,event_accept,FD_ACCEPT);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "WSAEventSelect failed: %d\n", WSAGetLastError());
			return -1;
		}

        
		// Post the AcceptEx(s)
		for(i=0; i < DEFAULT_ACCEPTEX_COUNT ;i++)
		{
			acceptobj = GetBufferObj();
			sockobj->recvobj = acceptobj;
			if(PostAccept(sockobj, acceptobj) != 0)
			{
				FreeBufferObj(acceptobj);
				fprintf(stderr, "PostAccept failed: %d\n", WSAGetLastError());
				return -1;
			}
		}


		endpointcount++;
		ptr = ptr->ai_next;
	}
	// free the addrinfo structure for the 'bind' address
	freeaddrinfo(res);

	DWORD sleeptime = CLOSE_DELAY / 2;				 //ÿ��sleep��ʱ��

#ifdef LOG_STATUS
	long		sock=0;
	long		buf=0;
#endif

	if(sleeptime < 50) //������50ms����
		sleeptime = 50;

	DWORD time_ontimer=GetTickCount();
	DWORD time_close = time_ontimer;
	DWORD time_alive = time_ontimer;
	DWORD newtime;
	BUFFER_OBJ *acceptobj;

	printf("\n>>> ��������ַ: %s: �˿ں�: %s <<<\n\n",
		local_ip, local_port);

	while (1)
	{
		rc = WaitForSingleObject(event_accept,sleeptime);
		if(rc == WAIT_FAILED)
		{
			fprintf(stderr,"WaitForSingleObject Failed:%d\n",GetLastError());
			break;
		} 
		else if(rc != WAIT_TIMEOUT) //GOT FD_ACCEPT
		{
		//	acceptobj = GetBufferObj();
		//	PostAccept(sockobj,acceptobj);
			if(m_accept_count < DEFAULT_ACCEPTEX_COUNT /2)
			{
				for(int i=0;i<DEFAULT_ACCEPTEX_COUNT/2;i++)
				{
					acceptobj = GetBufferObj();
					PostAccept(sockobj,acceptobj);
				}
			}
			ResetEvent(event_accept);
		}

		newtime = GetTickCount();

		if(newtime - time_ontimer > 3000) // 3��
		{ 
			OnTimer();  //call virtual timer();
			time_ontimer = newtime;			
#ifdef LOG_STATUS		//��ʾ������״̬
			if(sock != gTotalSock || buf != gTotalBuf)
			{
				Log_Server.Write("������״̬ : sock : %d ; buf : %d ; IoCount : %d ;",gTotalSock,gTotalBuf,gIoCount);

				sock = gTotalSock;
				buf = gTotalBuf;
			}
#endif

		}

		//Check Close
		if(newtime - time_close > sleeptime)
		{
			time_close = newtime;
			CheckClose();
		}

		if(newtime - time_alive > KEEPALIVE_TIME / 3)
		{
			CheckAlive();
			time_alive = newtime;
		}

	}

	WSACleanup();

	return 0;
}



//---------------------------------------------------------------------------------------------------------------------------
//CLConnect()
//NULL - connect failed .
//---------------------------------------------------------------------------------------------------------------------------
SOCKET_OBJ * CIocpServer::CLConnect(const char * ip,const char *port,int flag)
{
	SOCKET_OBJ *sockobj = NULL;
	BUFFER_OBJ *recvobj = NULL;
	struct addrinfo *res=NULL;
	bool   bFlag = false;

	res = ResolveAddress(ip, port, AF_INET,SOCK_STREAM, IPPROTO_TCP);
	if (res == NULL)
	{
		fprintf(stderr, "CLConnect ResolveAddress failed to return any addresses!\n");
		return NULL;
	}

	sockobj = GetSocketObj(INVALID_SOCKET);

	// create the socket
	sockobj->s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockobj->s == INVALID_SOCKET)
	{
		fprintf(stderr,"CLConnect socket failed: %d\n", WSAGetLastError());
		FreeSocketObj(sockobj);
		return NULL;
	}

#ifdef ENABLE_UNLIMIT_PORT
	SOCKADDR_IN local_addr={0};
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr  = ADDR_ANY;
	BOOL optvar = true;
	for(int i=0;i<40;i++)
	{
		local_addr.sin_port = htons(m_UnlimitPort);
		setsockopt(sockobj->s,SOL_SOCKET,SO_REUSEADDR,(char*)&optvar,sizeof(optvar));

		if(bind(sockobj->s,(SOCKADDR *)&local_addr,sizeof(SOCKADDR)) == -1)
		{
			if(m_UnlimitPort < UNLIMIT_PORT_END) 
				m_UnlimitPort++;
			else
				m_UnlimitPort = UNLIMIT_PORT_START;

			if(WSAGetLastError() != WSAEADDRINUSE )
			{ //bind������
				fprintf(stderr,"bind() failed: %d\n", WSAGetLastError());
				closesocket(sockobj->s);
				sockobj->s = INVALID_SOCKET;
				FreeSocketObj(sockobj);
				return NULL;
			}

		}
		else
			break;
	}

	if(m_UnlimitPort < UNLIMIT_PORT_END) 
		m_UnlimitPort++;
	else
		m_UnlimitPort = UNLIMIT_PORT_START;

#endif

	if( connect(sockobj->s,res->ai_addr,(int)res->ai_addrlen)!=SOCKET_ERROR )
	{
		if( CreateIoCompletionPort((HANDLE)sockobj->s, CLCompletionPort, (ULONG_PTR)sockobj, 0) == NULL)
			fprintf(stderr, "CLConnect : CLCreateIoCompletionPort failed: %d\n", GetLastError());
		else 
			bFlag = true;
	}

#ifdef LOG_LEVEL2
	if(!bFlag)
		Log_Server.Write("CLConnectʧ��: %d\n",WSAGetLastError());
#endif

	if(bFlag)
	{
		recvobj = GetBufferObj();

#ifdef DEBUG_IOCP
		recvobj->sclient = sockobj->s;
#endif

		LOCK(&sockobj->cs);

		if (PostRecv(sockobj, recvobj) != NO_ERROR)
		{
			UNLOCK(&sockobj->cs);
			FreeBufferObj(recvobj);
		}
		else
		{
			sockobj->flag = flag;
			sockobj->flag_accept = 1;
			sockobj->flag_close = 1;

			sockobj->recvobj = recvobj;

#ifdef ENABLE_KEEPALIVE
			AddAlive(sockobj,GetTickCount());
#endif

#ifdef DEBUG_IOCP
			sockobj->freeed = 0;
			sockobj->onclosed = 0;
#endif
			UNLOCK(&sockobj->cs);

			CLOnConnect(sockobj,sockobj->flag);

			return sockobj;
		}
	}

	closesocket(sockobj->s);
	sockobj->s = INVALID_SOCKET;
	FreeSocketObj(sockobj);

	return NULL;
}

int CIocpServer::CloseSock(SOCKET_OBJ *sock,bool close_delay) //�رյ�ǰ�Ŀͻ���
{
	if(sock == NULL)
		return -1;

#ifdef DEBUG_IOCP
	_ASSERTE(sock->onclosed == 0);	//�Ѿ����ù�OnClose()�ˣ�
	_ASSERTE(sock->freeed == 0);    //�Ѿ�free����!
#endif

	if( ( sock->flag_close == 0 ) || (sock->s == INVALID_SOCKET))
		return -1;

	LOCK(&sock->cs);

	if(sock->s != INVALID_SOCKET)
	{
		sock->flag_close = 0; //���ùرձ�־

		if(close_delay)
		{	  //��ʱ�ر�
			AddClose(sock,GetTickCount());
			shutdown(sock->s,SD_BOTH);
		}
		else 
		{			  //�����ر�
			SOCKET s = sock->s;
			sock->s = INVALID_SOCKET;
			closesocket(s);
		}
	}

	UNLOCK(&sock->cs);

	return 0;
}

void CIocpServer::OnTimer()
{

}

//������Ͷ��д���,Send()���ȳ��Խ�������ӵ����е����һ��BufferOBJ.���BufferOBJ�Ŀ��ÿռ䲻��,
//Send()������һ���µ�BufferOBJ���ڷ���.�����Ͷ���Ϊ��ʱ,Send()������PostSend()�����ύһ�����Ͳ���
//
int CIocpServer::Send(SOCKET_OBJ *sock,const void * buffer,int len)//������ͻ�������
{
	_ASSERTE(len <= DEFAULT_BUFFER_SIZE*MAX_UNSENDS_COUNT && len > 0);

	if(sock == NULL || len > DEFAULT_BUFFER_SIZE*MAX_UNSENDS_COUNT || len <= 0)
		return -1;

	BUFFER_OBJ * tmpbuf;
	int rc=NO_ERROR;

#ifdef DEBUG_IOCP
	_ASSERTE(sock->onclosed == 0); //�Ѿ����ù�OnClose()����?
	_ASSERTE(sock->freeed == 0);   //�Ѿ�free����!
#endif

	InterlockedIncrement(&sock->sending_count); //Ϊ�˰�ȫ��freeobj

	if( ( sock->flag_close == 0 ) || (sock->s == INVALID_SOCKET) )
	{
		InterlockedDecrement(&sock->sending_count);
		return -1;
	}

	int i=0;
	int len2=len;
	char *buf2;

	if(len2 > DEFAULT_BUFFER_SIZE)
		len2 = DEFAULT_BUFFER_SIZE;

	LOCK(&sock->cs);

	tmpbuf = sock->sendobj;
	if(tmpbuf == NULL)
	{ //���Ͷ���Ϊ��
		tmpbuf = GetBufferObj();

#ifdef DEBUG_IOCP
		tmpbuf->sclient = sock->s;
#endif

		memcpy(tmpbuf->buf,buffer,len2);
		tmpbuf->buflen = len2;

		rc = PostSend(sock,tmpbuf);
		if(rc == 0)
			sock->sendobj = tmpbuf;
		else
			FreeBufferObj(tmpbuf);

	}
	else
	{  
		while(tmpbuf->next)
		{
			tmpbuf = tmpbuf->next;
			i++;
		}

		if(i > MAX_UNSENDS_COUNT) 
		{
			rc = -1;
			CloseSock(sock);
		} 
		else
		{
			if(tmpbuf->buflen + len2 > DEFAULT_BUFFER_SIZE)
			{

				tmpbuf->next = GetBufferObj();
				tmpbuf = tmpbuf->next;
			}
			memcpy(tmpbuf->buf+tmpbuf->buflen,buffer,len2);
			tmpbuf->buflen += len2;
		}
	}

	len -= len2;
	buf2 = (char *)buffer+len2;

	while(rc == 0 && len >0)
	{
		len2 = len;

		if(len2 > DEFAULT_BUFFER_SIZE)
			len2 = DEFAULT_BUFFER_SIZE;

		tmpbuf->next = GetBufferObj();
		tmpbuf = tmpbuf->next;

		memcpy(tmpbuf->buf,buf2,len2);
		tmpbuf->buflen = len2;
		len -= len2;
		buf2 += len2;
		i++;

		if(i > MAX_UNSENDS_COUNT)
		{
#ifdef LOG_LEVEL1
			Log_Server.Write("Send(): ����ʧ��(��ǰ�û��ķ��Ͷ��г��� %d!",MAX_UNSENDS_COUNT);
#endif
			rc = -1;			
			break;
		}
	}

	UNLOCK(&sock->cs);

	InterlockedDecrement(&sock->sending_count);

	return rc;
	//ע��:��PostSend()����,buflen������ΪDEFAULT_BUFFER_SIZE,Send()����ʱ������ݲ�����ӵ����ڷ��͵�BufferObj����
}

//---------------------------------------------------------------------------------------------------------------------------
//HanelIO()
//---------------------------------------------------------------------------------------------------------------------------
void CIocpServer::HandleIo(SOCKET_OBJ *sock, BUFFER_OBJ *buf, 
						   HANDLE CompPort, DWORD BytesTransfered, DWORD error,
						   ADOConn *pAdo,bool isClient)
{

#ifdef LOG_STATUS
	InterlockedDecrement(&gIoCount);
#endif

	switch(buf->operation) 
	{	
	case OP_ACCEPT:		//����AcceptEx�ķ���	
		LOCK(&sock->cs);//����
		{
			if(error == 0)
			{			
				SOCKET_OBJ *clientobj = GetSocketObj(buf->sclient);//����һ���ͻ���sockobj
				HANDLE hrc = CreateIoCompletionPort(	           //��sockobj��ӵ���ɶ˿ڴ����б�	
					(HANDLE)buf->sclient,
					CompPort,
					(ULONG_PTR)clientobj,
					0
					);

				if (hrc != NULL) 
				{
					//��ȡip��ַ
					SOCKADDR *local=NULL;
					SOCKADDR *remote=NULL;
					int local_len;
					int remote_len;

					lpfnGetAcceptExSockaddrs(buf->buf,0,sizeof(SOCKADDR_STORAGE) + 16
						,sizeof(SOCKADDR_STORAGE) + 16,
						&local,&local_len,&remote,&remote_len);
					clientobj->addr = ((SOCKADDR_IN *)remote)->sin_addr;

#ifdef LOG_LEVEL1
					Log_Server.Write("--������ ip = %s , sock = %d ",GetAddress(clientobj),
						clientobj->s);
#endif

					BUFFER_OBJ *recvobj = GetBufferObj();
//���÷��ͻ���
#ifdef SEND_BUF_DISABLE
					BOOL sndbuf=0;
					setsockopt(buf->sclient,SOL_SOCKET,SO_SNDBUF,(char *)&sndbuf,sizeof(sndbuf));
#endif
					LOCK(&clientobj->cs);

					if(PostRecv(clientobj,recvobj) == 0)
					{ 
#ifdef DEBUG_IOCP
						clientobj->sockfd = clientobj->s;
						recvobj->sclient = clientobj->s;
#endif
						clientobj->recvobj = recvobj;
						AddAlive(clientobj,GetTickCount()); //������ӵ������б����һ���ͻ����������Ժ󲻷����ݣ��ᱻ�ߵ�
					}
					else
					{
						FreeBufferObj(recvobj);
						closesocket(clientobj->s);     //ע��رվ����clientobj->s�ľ����PostAcceptʱ�򴴽�
						clientobj->s = INVALID_SOCKET;
						FreeSocketObj(clientobj);
					}

					UNLOCK(&clientobj->cs);
				} 
				else
				{	//������ɶ˿�ʧ��,��newʧ��һ���������ϲ������
#ifdef LOG_LEVEL2
					Log_Server.Write("CreateIoCompletionPort ����:%d",GetLastError());
#endif
					closesocket(clientobj->s);
					clientobj->s = INVALID_SOCKET;
					FreeSocketObj(clientobj);
				}
			}
			else	//���AcceptEx���س���˵����һ���ͻ�������һ������ˣ���Ҫ�����ľ������.
				closesocket(buf->sclient);

			InterlockedDecrement(&m_accept_count);
			// һ�����������PostAccept
			if(m_accept_count < DEFAULT_ACCEPTEX_COUNT *2)
			{
				if(PostAccept(sock,buf)!=0)
					FreeBufferObj(buf);
			}
		}

		UNLOCK(&sock->cs);
		break;
	case OP_READ: //�յ�������
		{
			LOCK(&sock->cs); //��һ��

			bool bflag = false;
			_ASSERTE(buf == sock->recvobj);

			if(error == 0 && BytesTransfered > 0 )
			{ 
#ifdef LOG_LEVEL1  
				char head[256];
				sprintf(head,"(%d) :����",sock->s);
				if(isClient)
					strcat(head,"CL");
				Log_Server.WriteHex(buf->buf+buf->buflen,BytesTransfered,head,(int)strlen(head));
#endif

				buf->buflen += BytesTransfered; 
				int nret = 0; 

				if(isClient)
				{	//���ÿ������ص��麯��
					//������������Onϵ�к����е���Send(),CloseSock()ʱ���������
					UNLOCK(&sock->cs);
					nret = CLOnRead(sock,buf->buf,buf->buflen,sock->flag,pAdo);
					LOCK(&sock->cs);
				}
				else if(sock->flag_accept == 1)	
				{
					UNLOCK(&sock->cs);
					nret = OnRead(sock,buf->buf,buf->buflen,pAdo);
					LOCK(&sock->cs);
				}
				else
				{
					sock->flag_close = 1;  //���������־,�������Send����ʧ��
					sock->flag_accept = 1; //����accept��־
#ifndef ENABLE_KEEPALIVE		//û������KeepAlive	
					DeleteAlive(sock);
#endif
#ifdef DEBUG_IOCP
					sock->freeed = 0;
					sock->onclosed = 0;
#endif
					UNLOCK(&sock->cs);
					nret = OnAccept(sock,buf->buf,buf->buflen,pAdo);
					LOCK(&sock->cs);
				}

				_ASSERTE(nret >= 0 && nret <= buf->buflen);
				if(nret < 0 || nret > buf->buflen)
				{
#ifdef LOG_LEVEL2
					Log_Server.Write("ָ��������\n");
					nret = buf->buflen; //ǿ������Ϊ��ȫ����
#endif 
				}

#ifdef ENABLE_KEEPALIVE
				sock->timeAlive = GetTickCount(); //�����µ�����ʱ��
#endif
				buf->buflen -= nret;
				if(nret > 0 && buf->buflen > 0)
					memmove(buf->buf,buf->buf+nret,buf->buflen);

				if(PostRecv(sock,buf) == 0) //���µݽ�һ�����ղ���
					bflag = true;
			}

			if(!bflag)	
			{
				DeleteClose(sock); //�����Ƿ��������ʱ�ر��б���
				DeleteAlive(sock); //�����Ƿ�����������б���

				if(sock->s != INVALID_SOCKET) {
					closesocket(sock->s);
					sock->s = INVALID_SOCKET;
				}

				sock->flag_close = 0; //���ùرձ�־

				FreeBufferObj(buf);

				if( sock->flag_accept == 1) 
				{
					UNLOCK(&sock->cs);

					if(!isClient)
						OnClose(sock,pAdo); //����һ��OnClose(),�����ϲ�ĳ���,��Ҫ��ȥ�����sock��
					else
						CLOnClose(sock,sock->flag,pAdo);

					LOCK(&sock->cs);
#ifdef DEBUG_IOCP
					sock->onclosed = 1; //����ʱ���ùرձ�־,���ڼ���Ƿ�����߼�����
#endif
				}

				sock->recvobj = NULL;
				if(sock->recvobj == NULL && sock->sendobj == NULL) 
				{
					UNLOCK(&sock->cs);
					FreeSocketObj(sock); //�ͷŸÿͻ��˶�Ӧ��sockobj����
					return;
				}
			}

			UNLOCK(&sock->cs);
		}
		break;
	case OP_WRITE:
		LOCK(&sock->cs);
		{
			_ASSERTE(buf == sock->sendobj);
			bool bflag = false;
			BUFFER_OBJ *tmpobj = sock->sendobj;
			sock->sendobj = sock->sendobj->next;

			if(error == 0 && BytesTransfered > 0) //ǰһ�������Ѿ����
			{ 
#ifdef LOG_LEVEL1
				char head[256];
				sprintf(head,"(%d) :����",sock->s);
				if(isClient)
					strcat(head,"CL");
				Log_Server.WriteHex(buf->buf,BytesTransfered,head,(int)strlen(head));
#endif

				//��鷢�Ͷ���
				if(sock->sendobj == NULL)
					bflag = true;
				else if(PostSend(sock,sock->sendobj) == 0) 
					bflag = true;				
			}

			FreeBufferObj(tmpobj);

			if(!bflag)
			{
				sock->flag_close = 0; //���ùرձ�־
				while(sock->sendobj)
				{
					tmpobj = sock->sendobj;
					sock->sendobj = sock->sendobj->next;
					FreeBufferObj(tmpobj); 
				}

				if(sock->recvobj == NULL && sock->sendobj == NULL)
				{
					UNLOCK(&sock->cs);
					FreeSocketObj(sock); //���OP_READʱ,sock->sendobj!=NULL,��ô��Ҫ�������ͷſͻ��˵�SocketOBJ
					return;
				}
			}
		}

		UNLOCK(&sock->cs);
		break;
	}

}


int CIocpServer::OnAccept(SOCKET_OBJ *sock,const char *buf,int buflen,ADOConn *pAdo) 
{                                                     
	return buflen;
}
int  CIocpServer::OnRead(SOCKET_OBJ *sock,const char *buf,int buflen,ADOConn *pAdo) 
{	
	return buflen;
}

int CIocpServer::OnClose(SOCKET_OBJ *sock,ADOConn *pAdo)      
{
	return NO_ERROR;                                         
}

int CIocpServer::CLOnConnect(SOCKET_OBJ *sock,int flag)
{
	return NO_ERROR;
}

int  CIocpServer::CLOnRead(SOCKET_OBJ *sock,const char *buf,int buflen,int flag,ADOConn *pAdo) 
{													
	return buflen;
}

int CIocpServer::CLOnClose(SOCKET_OBJ *sock,int flag,ADOConn *pAdo)  
{                                                   
	return NO_ERROR;
}

// ��ɶ˿ڵĹ����߳�
unsigned int _stdcall CompletionThread(void * lpParam)
{
	SOCKET_OBJ  *sockobj=NULL;	// Per socket object for completed I/O
	BUFFER_OBJ  *bufobj=NULL;	// Per I/O object for completed I/O
	OVERLAPPED  *lpOverlapped=NULL;	// Pointer to overlapped structure for completed I/O
	HANDLE	CompletionPort;		// Completion port handle
	DWORD	BytesTransfered,	// Number of bytes transfered
		Flags;			// Flags for completed I/O
	int	rc, 
		error;

	bool isclient = false;
	CIocpServer * gIocp = (CIocpServer *)lpParam;
	CompletionPort = gIocp->CompletionPort;
	ADOConn *pAdo = NULL; 

	if(gIocp->m_bAdo & 1)
	{
		pAdo = new ADOConn();
		pAdo->OpenDatabase(gIocp->m_szAdo);
	}

	while (1)
	{
		error = NO_ERROR;

		lpOverlapped = NULL;
		rc = GetQueuedCompletionStatus(
			CompletionPort,
			&BytesTransfered,
			(PULONG_PTR)&sockobj,
			&lpOverlapped,
			INFINITE
			);
		if(lpOverlapped == NULL) 
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("CLCompletionThread: GetQueuedCompletionStatus failed(lpOverlapped == NULL): %d",GetLastError());
#endif
			continue;
		}
		bufobj = CONTAINING_RECORD(lpOverlapped, BUFFER_OBJ, ol);

		if (rc == FALSE) //����ʱ��ȡ�������
		{ 
			rc = WSAGetOverlappedResult(
				sockobj->s,
				&bufobj->ol,
				&BytesTransfered,
				FALSE,
				&Flags
				);
			if (rc == FALSE)
				error = WSAGetLastError();
			
		}

		gIocp->HandleIo(sockobj, bufobj, CompletionPort, BytesTransfered, error,pAdo,isclient);
	}

	if(pAdo)
		delete pAdo;

	_endthreadex(0);
	return 0;
}


// ��ɶ˿ڵĹ����߳�
unsigned int _stdcall CLCompletionThread(void * lpParam)
{
	SOCKET_OBJ  *sockobj=NULL;	// Per socket object for completed I/O
	BUFFER_OBJ  *bufobj=NULL;	// Per I/O object for completed I/O
	OVERLAPPED  *lpOverlapped=NULL;	// Pointer to overlapped structure for completed I/O
	HANDLE	CompletionPort;		// Completion port handle
	DWORD	BytesTransfered,	// Number of bytes transfered
		Flags;			// Flags for completed I/O
	int	rc, 
		error;

	bool isclient = true;

	CIocpServer * gIocp = (CIocpServer *)lpParam;
	CompletionPort = gIocp->CLCompletionPort;
	ADOConn *pAdo = NULL; 

	if(gIocp->m_bAdo & 2)
	{
		pAdo = new ADOConn();
		pAdo->OpenDatabase(gIocp->m_szAdo);
	}

	while (1)
	{
		error = NO_ERROR;
		lpOverlapped = NULL;
		rc = GetQueuedCompletionStatus(
			CompletionPort,
			&BytesTransfered,
			(PULONG_PTR)&sockobj,
			&lpOverlapped,
			INFINITE
			);

		if(lpOverlapped == NULL)	
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("CLCompletionThread: GetQueuedCompletionStatus failed(lpOverlapped == NULL): %d",GetLastError());
#endif
			continue;
		}
		bufobj = CONTAINING_RECORD(lpOverlapped, BUFFER_OBJ, ol);

		if (rc == FALSE)
		{ //����ʱ��ȡ�������
			rc = WSAGetOverlappedResult(
				sockobj->s,
				&bufobj->ol,
				&BytesTransfered,
				FALSE,
				&Flags
				);
			if (rc == FALSE)
				error = WSAGetLastError();
			
		}

		gIocp->HandleIo(sockobj, bufobj, CompletionPort, BytesTransfered, error,pAdo,isclient);
	}

	if(pAdo)
		delete pAdo;

	_endthreadex(0);
	return 0;
}

