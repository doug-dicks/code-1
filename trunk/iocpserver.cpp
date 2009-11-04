//  宋体 , TAB = 8空格
#include "stdafx.h"
#include "iocpserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <process.h>

/* 内部参数  -------------------------------------------------------------------------------------------------*/
#define MAX_UNSENDS_COUNT        100	// Maximun send list.
#define DEFAULT_ACCEPTEX_COUNT   100	// Maximum number of acceptex count

#define SPIN_COUNT_SOCK		64	//用于sockobj的spin count
#define SPIN_COUNT_ALIVE	64	//用于Alive列表的spin count
#define SPIN_COUNT_CLOSE	64	//用于Close列表的spin count	
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
	//初始化内存池对象
	m_sockobj = new cFixedMemoryPool(sizeof(SOCKET_OBJ),(int)(AVERAGE_CONNECTIONS*2),1);
	m_bufobj = new cFixedMemoryPool(sizeof(BUFFER_OBJ),(int)(AVERAGE_CONNECTIONS*3));
	m_buffer  =  new cFixedMemoryPool(DEFAULT_BUFFER_SIZE,(int)(AVERAGE_CONNECTIONS*3));

	InitializeCriticalSectionAndSpinCount(&secAlive,SPIN_COUNT_ALIVE);
	InitializeCriticalSectionAndSpinCount(&secClose,SPIN_COUNT_CLOSE);

	m_Inited = false;
	CompletionPort = INVALID_HANDLE_VALUE;
	CLCompletionPort = INVALID_HANDLE_VALUE;
	m_accept_count = 0;

#ifdef LOG_ENABLE	//根据可执行文件名,自动创建一个日志文件.
	char log_name[512];
	char *pname;
	GetModuleFileName(NULL,log_name,sizeof(log_name));
	pname = log_name+strlen(log_name) -1;
	*pname = 't';pname--;
	*pname = 'x';pname--;
	*pname = 't';
	if(Log_Server.Init(log_name))
		printf("创建Log:\"%s\"   ",log_name);
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

		printf("正在关闭...");

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

	//创建BUFFER_OBJ对象
	newobj = (BUFFER_OBJ *)m_bufobj->Alloc();
	//创建BUFFER_OBJ中的buf对象
	pbuf = (char*)m_buffer->Alloc();

	//初始化
	ZeroMemory(newobj,sizeof(BUFFER_OBJ));
	//ZeroMemory(pbuf,DEFAULT_BUFFER_SIZE);  //数据内容可以不必初始化
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

#ifdef DEBUG_IOCP	//判断这个sock是否已经被释放了
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

	while(obj->sending_count != 0) //安全free sockobj
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
	//添加到心跳列表
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
	//从列表中删除这个对象
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
	//检查超时
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
			s = ittmp->first->s;				//先把SOCKOBJ的s设为INVALID_SOCKET
			ittmp->first->s = INVALID_SOCKET;	//再关闭句柄,为了线程安全
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
	//添加到延时关闭列表
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
	//从列表中删除
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
	//检查延时关闭超时
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
			s = ittmp->first->s;			 //先把SOCKOBJ的s设为INVALID_SOCKET
			ittmp->first->s = INVALID_SOCKET;//然后调用关闭可以保证线程安全
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
		Log_Server.Write("(->%d)接收缓冲溢出",sock->s);
#endif
		CloseSock(sock);
		return -1;
	}

	recvobj->operation = OP_READ;

	wbuf.buf = recvobj->buf+recvobj->buflen;			//将接收缓冲定位到原来数据的末尾
	wbuf.len = DEFAULT_BUFFER_SIZE - recvobj->buflen;	//重新设置可接受数据的长度

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
			Log_Server.Write("(->%d)PostRecv错误: %d",sock->s,WSAGetLastError());
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
	sendobj->buflen = DEFAULT_BUFFER_SIZE;	//设置为最大值，免得调用Send()时候将数据追加到这个bufferobj
											//Send函数会首先判断原有队列中buffer数据的内容，如果小于DEFAULT_BUFFER_SIZE
											//发送的数据将会加到前一个buffer obj后面

	if (rc == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING) 
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("(->%d)PostSend错误: %d",sock->s,WSAGetLastError());
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
		Log_Server.Write("PostAccept(socket())错误: %d",WSAGetLastError());
#endif 			rc = -1;
	}
	else
	{
		rc = lpfnAcceptEx(
			sock->s,
			acceptobj->sclient,
			acceptobj->buf,
			0,				//acceptobj->buflen - ((sizeof(SOCKADDR_STORAGE) + 16) * 2),
			//把缓冲大小设置为0，可以使Accept接收到连接就立即返回。
			sizeof(SOCKADDR_STORAGE) + 16,
			sizeof(SOCKADDR_STORAGE) + 16,
			&bytes,
			&acceptobj->ol
			);
		if (rc == FALSE && WSAGetLastError() != WSA_IO_PENDING)
		{
#ifdef LOG_LEVEL2
			Log_Server.Write("(->%d)PostAccept错误: %d",sock->s,WSAGetLastError());
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

	//创建用于连接的完成端口
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
	//创建用于监听的完成端口句柄
	CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
	if (CompletionPort == NULL) 
	{
		fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
		return -1;
	}

	//创建工作线程
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
		fprintf(stderr, "初始化过一次了!\n");
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

	printf("\n\n连接线程:%d: 监听线程:%d; 平均连接数:%d ; ",
		m_CLThreads,m_Threads,AVERAGE_CONNECTIONS);
	printf("IOCP版本: %s;\n",IOCP_VERSION);

#ifdef ENABLE_KEEPALIVE
	printf("心跳检测:开启; ");
#else
	printf("心跳检测:关闭; ");
#endif
	printf("心跳超时:%d; 关闭延时:%d\n",KEEPALIVE_TIME,CLOSE_DELAY);


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

	DWORD sleeptime = CLOSE_DELAY / 2;				 //每次sleep的时间

#ifdef LOG_STATUS
	long		sock=0;
	long		buf=0;
#endif

	if(sleeptime < 50) //限制在50ms以外
		sleeptime = 50;

	DWORD time_ontimer=GetTickCount();
	DWORD time_close = time_ontimer;
	DWORD time_alive = time_ontimer;
	DWORD newtime;
	BUFFER_OBJ *acceptobj;

	printf("\n>>> 服务器地址: %s: 端口号: %s <<<\n\n",
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

		if(newtime - time_ontimer > 3000) // 3秒
		{ 
			OnTimer();  //call virtual timer();
			time_ontimer = newtime;			
#ifdef LOG_STATUS		//显示服务器状态
			if(sock != gTotalSock || buf != gTotalBuf)
			{
				Log_Server.Write("服务器状态 : sock : %d ; buf : %d ; IoCount : %d ;",gTotalSock,gTotalBuf,gIoCount);

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
			{ //bind出错了
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
		Log_Server.Write("CLConnect失败: %d\n",WSAGetLastError());
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

int CIocpServer::CloseSock(SOCKET_OBJ *sock,bool close_delay) //关闭当前的客户端
{
	if(sock == NULL)
		return -1;

#ifdef DEBUG_IOCP
	_ASSERTE(sock->onclosed == 0);	//已经调用过OnClose()了！
	_ASSERTE(sock->freeed == 0);    //已经free掉了!
#endif

	if( ( sock->flag_close == 0 ) || (sock->s == INVALID_SOCKET))
		return -1;

	LOCK(&sock->cs);

	if(sock->s != INVALID_SOCKET)
	{
		sock->flag_close = 0; //设置关闭标志

		if(close_delay)
		{	  //延时关闭
			AddClose(sock,GetTickCount());
			shutdown(sock->s,SD_BOTH);
		}
		else 
		{			  //立即关闭
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

//如果发送队列存在,Send()首先尝试将数据添加到队列的最后一个BufferOBJ.如果BufferOBJ的可用空间不足,
//Send()将开辟一个新的BufferOBJ用于发送.当发送队列为空时,Send()将调用PostSend()立即提交一个发送操作
//
int CIocpServer::Send(SOCKET_OBJ *sock,const void * buffer,int len)//发送向客户端数据
{
	_ASSERTE(len <= DEFAULT_BUFFER_SIZE*MAX_UNSENDS_COUNT && len > 0);

	if(sock == NULL || len > DEFAULT_BUFFER_SIZE*MAX_UNSENDS_COUNT || len <= 0)
		return -1;

	BUFFER_OBJ * tmpbuf;
	int rc=NO_ERROR;

#ifdef DEBUG_IOCP
	_ASSERTE(sock->onclosed == 0); //已经调用过OnClose()还发?
	_ASSERTE(sock->freeed == 0);   //已经free掉了!
#endif

	InterlockedIncrement(&sock->sending_count); //为了安全的freeobj

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
	{ //发送队列为空
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
			Log_Server.Write("Send(): 发送失败(当前用户的发送队列超出 %d!",MAX_UNSENDS_COUNT);
#endif
			rc = -1;			
			break;
		}
	}

	UNLOCK(&sock->cs);

	InterlockedDecrement(&sock->sending_count);

	return rc;
	//注意:在PostSend()里面,buflen被设置为DEFAULT_BUFFER_SIZE,Send()调用时候的数据不会添加到正在发送的BufferObj后面
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
	case OP_ACCEPT:		//处理AcceptEx的返回	
		LOCK(&sock->cs);//加锁
		{
			if(error == 0)
			{			
				SOCKET_OBJ *clientobj = GetSocketObj(buf->sclient);//创建一个客户端sockobj
				HANDLE hrc = CreateIoCompletionPort(	           //将sockobj添加到完成端口处理列表	
					(HANDLE)buf->sclient,
					CompPort,
					(ULONG_PTR)clientobj,
					0
					);

				if (hrc != NULL) 
				{
					//读取ip地址
					SOCKADDR *local=NULL;
					SOCKADDR *remote=NULL;
					int local_len;
					int remote_len;

					lpfnGetAcceptExSockaddrs(buf->buf,0,sizeof(SOCKADDR_STORAGE) + 16
						,sizeof(SOCKADDR_STORAGE) + 16,
						&local,&local_len,&remote,&remote_len);
					clientobj->addr = ((SOCKADDR_IN *)remote)->sin_addr;

#ifdef LOG_LEVEL1
					Log_Server.Write("--新连接 ip = %s , sock = %d ",GetAddress(clientobj),
						clientobj->s);
#endif

					BUFFER_OBJ *recvobj = GetBufferObj();
//禁用发送缓冲
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
						AddAlive(clientobj,GetTickCount()); //把他添加到心跳列表，如果一个客户端连上来以后不发数据，会被踢掉
					}
					else
					{
						FreeBufferObj(recvobj);
						closesocket(clientobj->s);     //注意关闭句柄，clientobj->s的句柄在PostAccept时候创建
						clientobj->s = INVALID_SOCKET;
						FreeSocketObj(clientobj);
					}

					UNLOCK(&clientobj->cs);
				} 
				else
				{	//创建完成端口失败,像new失败一样，基本上不会出现
#ifdef LOG_LEVEL2
					Log_Server.Write("CreateIoCompletionPort 错误:%d",GetLastError());
#endif
					closesocket(clientobj->s);
					clientobj->s = INVALID_SOCKET;
					FreeSocketObj(clientobj);
				}
			}
			else	//如果AcceptEx返回出错，说明有一个客户端连了一半就退了，需要把他的句柄关了.
				closesocket(buf->sclient);

			InterlockedDecrement(&m_accept_count);
			// 一般情况下重新PostAccept
			if(m_accept_count < DEFAULT_ACCEPTEX_COUNT *2)
			{
				if(PostAccept(sock,buf)!=0)
					FreeBufferObj(buf);
			}
		}

		UNLOCK(&sock->cs);
		break;
	case OP_READ: //收到数据了
		{
			LOCK(&sock->cs); //锁一下

			bool bflag = false;
			_ASSERTE(buf == sock->recvobj);

			if(error == 0 && BytesTransfered > 0 )
			{ 
#ifdef LOG_LEVEL1  
				char head[256];
				sprintf(head,"(%d) :接收",sock->s);
				if(isClient)
					strcat(head,"CL");
				Log_Server.WriteHex(buf->buf+buf->buflen,BytesTransfered,head,(int)strlen(head));
#endif

				buf->buflen += BytesTransfered; 
				int nret = 0; 

				if(isClient)
				{	//调用可以重载的虚函数
					//开锁，否则在On系列函数中调用Send(),CloseSock()时候会死锁！
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
					sock->flag_close = 1;  //设置允许标志,否则调用Send将会失败
					sock->flag_accept = 1; //设置accept标志
#ifndef ENABLE_KEEPALIVE		//没有启用KeepAlive	
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
					Log_Server.Write("指令处理出错啦\n");
					nret = buf->buflen; //强制设置为完全处理
#endif 
				}

#ifdef ENABLE_KEEPALIVE
				sock->timeAlive = GetTickCount(); //设置新的心跳时间
#endif
				buf->buflen -= nret;
				if(nret > 0 && buf->buflen > 0)
					memmove(buf->buf,buf->buf+nret,buf->buflen);

				if(PostRecv(sock,buf) == 0) //重新递交一个接收操作
					bflag = true;
			}

			if(!bflag)	
			{
				DeleteClose(sock); //看看是否存在于延时关闭列表中
				DeleteAlive(sock); //看看是否存在于心跳列表中

				if(sock->s != INVALID_SOCKET) {
					closesocket(sock->s);
					sock->s = INVALID_SOCKET;
				}

				sock->flag_close = 0; //设置关闭标志

				FreeBufferObj(buf);

				if( sock->flag_accept == 1) 
				{
					UNLOCK(&sock->cs);

					if(!isClient)
						OnClose(sock,pAdo); //调用一次OnClose(),告诉上层的程序,不要再去用这个sock了
					else
						CLOnClose(sock,sock->flag,pAdo);

					LOCK(&sock->cs);
#ifdef DEBUG_IOCP
					sock->onclosed = 1; //调试时设置关闭标志,用于检测是否存在逻辑问题
#endif
				}

				sock->recvobj = NULL;
				if(sock->recvobj == NULL && sock->sendobj == NULL) 
				{
					UNLOCK(&sock->cs);
					FreeSocketObj(sock); //释放该客户端对应的sockobj对象
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

			if(error == 0 && BytesTransfered > 0) //前一个发送已经完成
			{ 
#ifdef LOG_LEVEL1
				char head[256];
				sprintf(head,"(%d) :发送",sock->s);
				if(isClient)
					strcat(head,"CL");
				Log_Server.WriteHex(buf->buf,BytesTransfered,head,(int)strlen(head));
#endif

				//检查发送队列
				if(sock->sendobj == NULL)
					bflag = true;
				else if(PostSend(sock,sock->sendobj) == 0) 
					bflag = true;				
			}

			FreeBufferObj(tmpobj);

			if(!bflag)
			{
				sock->flag_close = 0; //设置关闭标志
				while(sock->sendobj)
				{
					tmpobj = sock->sendobj;
					sock->sendobj = sock->sendobj->next;
					FreeBufferObj(tmpobj); 
				}

				if(sock->recvobj == NULL && sock->sendobj == NULL)
				{
					UNLOCK(&sock->cs);
					FreeSocketObj(sock); //如果OP_READ时,sock->sendobj!=NULL,那么需要在这里释放客户端的SocketOBJ
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

// 完成端口的工作线程
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

		if (rc == FALSE) //出错时读取错误代码
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


// 完成端口的工作线程
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
		{ //出错时读取错误代码
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

