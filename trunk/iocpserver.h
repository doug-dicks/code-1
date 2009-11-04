//  宋体 , TAB = 8空格
/////////////////////////////////////////////////////////////////////////////
//  The IOComptionPort Libaray     2.0 
//              Copyright ?2005 Written by ZhaoBin
//
//  include <CLog,cMemoryPool,ADOConn>
//
//  CLog by CaiWeiWei
//  cFixedMemoryPool by XuDuo
//
//  Finished 2005/03/09
//
//  Last updated 2005/06/16
//
//  Fix:
//  2005/06/16
//  1,修正FD_ACCEPT问题
//
//  2005/04/27
//  1,增加GetAddress()接口,用于读取客户端的ip
//  2,关闭tcp/ip发送缓冲,减少内存copy
//  3,SOCKET_OBJ的标志改为用位段实现
//
//  2005/03/21
//  1,显示版本信息
//  2,消除"无限连"时出现大量TimeWait状态的问题.
//  3,增加了"无限连",Connect不再有3000左右的端口限制
//
/////////////////////////////////////////////////////////////////////////////

#ifndef _IOCPSERVER_H
#define _IOCPSERVER_H
#define _WIN32_WINNT 0x0403 

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <fstream>
#include <map>
#include <crtdbg.h>

#define IOCP_VERSION "2005-06-16"

//自定义选项  -------------------------------------------------------------------------------------------
const WORD AVERAGE_CONNECTIONS = 600;	//平均连接数(这个值和程序性能有关)
#define SEND_BUF_DISABLE		//禁用发送缓冲,有助于减少memcpy()次数,但是会使发送速度变慢.
					//处理大量连接时禁用缓冲可以提高性能.

#define LOG_ENABLE			//启用日志功能
//#define LOG_LEVEL2			//记录其他信息(连接失败,发送失败等错误信息)
#define LOG_LEVEL1			//记录发送和接收的数据

//#define ENABLE_KEEPALIVE		//启用心跳检测.启用时稍微影响一点效率
const DWORD KEEPALIVE_TIME  = 60*1000;	//心跳检测时间
const DWORD CLOSE_DELAY	= 1200;		//调用了CloseSock()以后,等待1200ms再调用真正的关闭函数

#define DEBUG_IOCP			//对一个已经OnClose()的sock调用Send,CloseSock将会引发_ASSERTE
#define DEBUG_ADO			//读取数据项失败时出现_ASSERTE.注意:查询失败不会出现_ASSERTE

//#define ENABLE_UNLIMIT_PORT		//启用无限连.CLConnect()个数超过3000时使用
//------------------------------------------------------------------------------------------------------

using namespace std;

#pragma comment (lib,"ws2_32.lib")

const WORD DEFAULT_BUFFER_SIZE = 4096;
const WORD UNLIMIT_PORT_START = 13000;	//无限连的起始端口
const WORD UNLIMIT_PORT_END = 50000;	//无限连的最高端口	

class CLog;

#ifdef LOG_ENABLE
extern CLog Log_Server;
#define LOG_STATUS
#endif


class CLog 
{
public:
	void Write(char *format,...);
	void WriteHex(char *buf, int buflen,char *head,int headlen);
	void SetOpt(bool file,bool print,bool debugstr,bool time,bool idx);
	bool Init(char *name);
	CLog();
	virtual ~CLog();

private:
	fstream m_file;
	bool m_bFile;
	bool m_bPrint;
	bool m_bDebugString;
	bool m_bTime;
	bool m_bIdx;
	bool m_bInit;
	char m_filename[512];
	DWORD m_WriteTimes;

	int   m_idx;
	char  m_buf[4096*10];
	CRITICAL_SECTION m_sec;
};


//-------------------------------------------------------------------------------
//	Pandora Box Game Development Kit(PBGDK)
//
//	pbMemoryPool.h
//	内存池处理的函数
//	
//	Version 2.5
//	
//	Copyright (C) Ryo, 2004,2005
//	
//	Date created 2004/11/18
//	Last updated 2005/02/23
//	
//-------------------------------------------------------------------------------

#ifndef _PB_MEMORYPOOL_H
#define _PB_MEMORYPOOL_H

//------------------------------------------------------------------------------
//	这个类主要用于检测池操作的健康性
//
//------------------------------------------------------------------------------
class	cFixedMemoryPool;
class	cFixedMemoryPoolTracking 
{
public:
	static void * _pool_memcpy( const cFixedMemoryPool *, void * pDst, const void * pSrc, int size );	//	这个函数用来检测关于内存池的操作
	static void * _pool_memset( const cFixedMemoryPool *, void * pDst, int value, int size );
	static void * _pool_memmove( const cFixedMemoryPool *, void * pDst, const void * pSrc, int size );
};

//------------------------------------------------------------------------------
//	宏定义
//	@使用宏定义可以记录下分配水滴的位置和来源，可以用于快速的定位内存泄漏的具体位置
//	******************** 建议使用宏来操作内存池以确保内存池的健壮性 ************************
//------------------------------------------------------------------------------
#ifdef _DEBUG_POOL
#define	_POOL_ALLOC( a )		a.Alloc( __FILE__, __LINE__ )
#define _POOL_ALLOC_PTR( a )		a->Alloc( __FILE__, __LINE__ )
#define	_POOL_MEMCPY( pool, a, b, c )	cFixedMemoryPoolTracking::_pool_memcpy( pool, a, b, (int)c )
#define	_POOL_MEMSET( pool, a, b, c )	cFixedMemoryPoolTracking::_pool_memset( pool, a, b, (int)c )
#define	_POOL_MEMMOVE( pool, a, b, c )	cFixedMemoryPoolTracking::_pool_memmove( pool, a, b, (int)c )
#else
#define	_POOL_ALLOC( a )		a.Alloc()
#define _POOL_ALLOC_PTR( a )		a->Alloc()
#define	_POOL_MEMCPY( pool, a, b, c )	memcpy( a, b, c )
#define	_POOL_MEMSET( pool, a, b, c )	memset( a, b, c )
#define	_POOL_MEMMOVE( pool, a, b, c )	memmove( a, b, c )
#endif

//------------------------------------------------------------------------------
//	水滴的定义
//
//------------------------------------------------------------------------------
struct t_Blob 
{
	t_Blob	* m_pNext;

#ifdef _DEBUG_POOL
	t_Blob	* m_pPrior;	//	指向上一水滴的位置
	void	* m_pPoolFather;//	指向源内存池的指针,这个参数用来监测水滴和水池是否相匹配
	LPCSTR	m_szFileName;	//	分配该水滴来源的文件
	DWORD	m_dwLine;	//	运行行数的位置
	DWORD	m_dwFreeFlag;	//	释放的标志位,避免重复释放
#endif
};

//------------------------------------------------------------------------------
//	池的类定义(固定长度)
//
//------------------------------------------------------------------------------
class cFixedMemoryPool 
{
	friend class cFixedMemoryPoolTracking;

public:
	cFixedMemoryPool();
	cFixedMemoryPool( int iBlobSize, int iMaxBlob,int Flag = 0 );
	virtual ~cFixedMemoryPool();

	virtual	int	Initialize( int iBlobSize, int iMaxBlob );
	virtual	int	Release	();		//释放

#ifdef _DEBUG_POOL
	virtual	void *	Alloc	( LPCSTR szFile = __FILE__, DWORD dwFileLine = __LINE__ );
#else
	virtual	void *	Alloc	();		//分配
#endif

	virtual int Free( const void * ptr );	//释放

private:

	int	m_iBlobSize;		//水滴的大小
	int	m_iFreeBlob;		//目前剩余的水滴数量
	int	m_iMaxBlob;		//池最大可容纳的水滴数量
	int	m_Flag;			//额外创建的对象是否调用free
	t_Blob	* m_pFreeBlob;		//已经被释放的内存节点.
	t_Blob	* m_pSelectedBlob;
	t_Blob	** m_pBlobPtr;		//水滴指针的数组位置
	char	* m_pData;

	char	* m_pBeginAddress;
	char	* m_pEndAddress;

	CRITICAL_SECTION	m_sec;	

#ifdef _DEBUG_POOL
	t_Blob	* m_pLeakBlob;		//池外分配的水滴指针
#endif
};

#endif _PB_MEMORYPOOL_H


//定义数据库连接类ADOConn

#if !defined(AFX_ADOCONN_H__AC448F02_AF26_45E4_9B2D_D7ECB8FFCFB9__INCLUDED_)
#define AFX_ADOCONN_H__AC448F02_AF26_45E4_9B2D_D7ECB8FFCFB9__INCLUDED_
#import "c:\Program Files\Common Files\System\ado\msado15.dll" no_namespace rename("EOF","adoEOF") rename("BOF","adoBOF")


class ADOConn  
{
public:
	ADOConn();
	virtual ~ADOConn();
	// 初始化—连接数据库
	bool  OpenDatabase(char *SqlConnectStr=NULL);
	// 执行查询
	int GetRecord(_bstr_t bstrSQL);	// 0 - 成功执行，1 - 记录为空, -1 执行数据库操作失败  

	inline void MoveNext()
	{
		m_pRecordset->MoveNext();
	};
	inline VARIANT_BOOL bEof()
	{
		if(bHaveRecord)
			return m_pRecordset->adoEOF;
		else
			return true;
	};
	void CloseRecord();

	// 执行SQL语句，Insert Update _variant_t
	int ExecuteSQL(_bstr_t bstrSQL); // 0 - 成功执行，1 - 影响0条记录, -1 执行数据库操作失败  

	//读取记录内容
	bool GetItem(const char *ItemName,int *Item);
	bool GetItem(const char *ItemName,DWORD *Item);
	bool GetItem(const char *ItemName,WORD *Item);
	bool GetItem(const char *ItemName,BYTE *Item);
	bool GetItem(const char *ItemName,bool *Item);
	bool GetItem(const char *ItemName,char *buf,int buflen);

private:
	//添加一个指向Connection对象的指针:
	_ConnectionPtr m_pConnection;
	//添加一个指向Recordset对象的指针:
	_RecordsetPtr m_pRecordset;
	_bstr_t m_strConnect;
	bool bHaveRecord;
};
#endif

typedef struct _BUFFER_OBJ
{
	WSAOVERLAPPED ol;

	SOCKET sclient;		//Only used for AcceptEx client socket

	char   *buf;		// Buffer for recv/send/AcceptEx
	int    buflen;		// Length of the buffer
	int    operation;	// Type of operation issued

	struct _BUFFER_OBJ *next;
} BUFFER_OBJ;

union PRIVATE_DATA 
{
	DWORD dwVar;		//dword类型
	long  lVar;		//long类型
	int   iVar;		//int类型  		
	void  *pVar;		//指针类型
};


typedef struct _SOCKET_OBJ
{
	volatile SOCKET	s;		// Socket handle
	in_addr  addr;			// 客户端的ip地址
	volatile DWORD timealive;	// For keepalive check

	volatile BYTE flag_alive:1;	//KeepAlive标志,1-有效
	volatile BYTE flag_delay_close:1;//延时关闭标志,1-有效
	volatile BYTE flag_accept:1;	//OnAccept()标志,1-表示已经调用了OnAccept
	volatile BYTE flag_close:1;	//Close标志,1-sokcet未关闭

	volatile DWORD flag;		

	volatile long sending_count;	// 正在调用Send()的个数

	BUFFER_OBJ *sendobj;
	BUFFER_OBJ *recvobj;

	CRITICAL_SECTION cs;		// Protect access to this structure

	PRIVATE_DATA myData;	

#ifdef DEBUG_IOCP			//调试信息
	BUFFER_OBJ *lastsend;		//最近一次的发送obj
	int onclosed;			//是否调用了OnClose();
	SOCKET sockfd;			//socket描述符的值
	int freeed;			//是否已经被free			
#endif
} SOCKET_OBJ;

class CIocpServer 
{
public :
	CIocpServer();
	virtual ~CIocpServer();

	// 初始化
	// ConnectThread : 作为客户端连接其他服务器时的线程数
	// ListenThread : 监听的线程数
	// szConnect : 数据库连接字符串
	// ADOFlag : 01(二进制)-只启动监听线程的数据库连接 , 
	//           10(二进制)- 连接线程 ,11(二进制)-同时,0 - 不使用ado
	int Init(int connect_thread,int listen_thread,int adoflag = 0,char *adostring = NULL);

	int StartServer(const char *local_ip,const char *local_port); //开始监听

	int Send(SOCKET_OBJ *sock,const void *buffer,int len);
	int CloseSock(SOCKET_OBJ *sock,bool close_delay = false); //关闭连接
	inline const char * GetAddress(SOCKET_OBJ *sock) //取得某个客户端的ip地址(字符串)
	{
		return inet_ntoa(sock->addr);
	}
	inline in_addr GetAddressEx(SOCKET_OBJ *sock) //取得某个客户端的ip地址(in_addr)
	{
		return sock->addr;
	}

	// int flag : 连接标志.变量值等于CLConnect()时的flag参数.
	// 在同时连接了多个服务器时,可以根据flag判断当前sock对应的服务器连接.
	SOCKET_OBJ * CLConnect(const char *ip,const char *port,int flag); 	

	virtual void OnTimer();

	// SOCKET_OBJ *sock : 当前连接的对象,Send(),CloseSock()的调用需要该参数.其中
	//                    的sock->myData可以自由使用,IOCP的调用不会改变他的内容.
	//                    可以在用户名和密码认证通过时,将sock-myData与用户信息绑定.
	//                    然后根据sock快速定位用户信息.
	//
	// const char *buf : 接收缓冲
	// int buflen : 接收缓冲的数据长度
	// ADOConn *pAdo : 每个线程对应的数据库对象,Init()调用启用了数据连接时有效,
	//                 否则返回NULL.
	//
	// 返回值: 返回处理完的数据长度(OnClose(),CLOnClose()返回0).
	//         例如: 在一次OnRead()中收到了2.5条指令,处理了2条指令以后,返回2条指令
	//         的buf长度,最后的0.5条指令将随下一次的OnRead()返回.
	virtual int OnAccept(SOCKET_OBJ *sock,const char *buf,int buflen,ADOConn *pAdo); 
	virtual int OnRead(SOCKET_OBJ *sock,const char *buf,int buflen,ADOConn *pAdo); 
	virtual int OnClose(SOCKET_OBJ *sock,ADOConn *pAdo);  //可以返回任意值

	virtual int CLOnConnect(SOCKET_OBJ *sock,int flag);   //可以返回任意值
	virtual int CLOnRead(SOCKET_OBJ *sock,const char *buf,int buflen,int flag,ADOConn *pAdo);
	virtual int CLOnClose(SOCKET_OBJ *sock,int flag,ADOConn *pAdo); //可以返回任意值

private:
	LPFN_ACCEPTEX lpfnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;

	cFixedMemoryPool *m_sockobj;
	cFixedMemoryPool *m_bufobj;
	cFixedMemoryPool *m_buffer;

	int m_CLThreads;
	int m_Threads;  
	int m_bAdo;
	_bstr_t m_szAdo;

	bool m_Inited;

	volatile long m_accept_count;

	WORD m_UnlimitPort; 

	int InitClient(int threadnum);

	int InitServer(int threadnum);

	SOCKET_OBJ *GetSocketObj(SOCKET s);
	void FreeSocketObj(SOCKET_OBJ *obj);

	BUFFER_OBJ *GetBufferObj();
	void FreeBufferObj(BUFFER_OBJ *obj);

	struct addrinfo *ResolveAddress(const char *addr,const char *port, int af, int type, int proto);

	void HandleIo(SOCKET_OBJ *sock, BUFFER_OBJ *buf, HANDLE CompPort, 
		DWORD BytesTransfered, DWORD error,ADOConn *pAdo,bool isclient);

	HANDLE CompletionPort;
	HANDLE CLCompletionPort;

	//可以做成一个函数，但是比较麻烦，还不如分开来写.
	friend unsigned int _stdcall CompletionThread(void * lpParam);  //工作线程
	friend unsigned int _stdcall CLCompletionThread(void * lpParam);//工作线程

	int PostRecv(SOCKET_OBJ *sock, BUFFER_OBJ *recvobj);
	int PostSend(SOCKET_OBJ *sock, BUFFER_OBJ *sendobj);
	int PostAccept(SOCKET_OBJ *sock, BUFFER_OBJ *acceptobj);

	inline void AddAlive(SOCKET_OBJ *sock,DWORD time); //添加到心跳列表
	inline void DeleteAlive(SOCKET_OBJ *sock);//从列表中删除
	void CheckAlive();//检查是否存在超时

	inline void AddClose(SOCKET_OBJ *sock,DWORD time);
	inline void DeleteClose(SOCKET_OBJ *sock);
	void CheckClose();//检查关闭超时 

	map<SOCKET_OBJ *,DWORD> m_mapAlive;
	CRITICAL_SECTION secAlive;

	map<SOCKET_OBJ *,DWORD> m_mapClose;
	CRITICAL_SECTION secClose;
};


#endif

















