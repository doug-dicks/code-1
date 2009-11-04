//  ���� , TAB = 8�ո�
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
//  1,����FD_ACCEPT����
//
//  2005/04/27
//  1,����GetAddress()�ӿ�,���ڶ�ȡ�ͻ��˵�ip
//  2,�ر�tcp/ip���ͻ���,�����ڴ�copy
//  3,SOCKET_OBJ�ı�־��Ϊ��λ��ʵ��
//
//  2005/03/21
//  1,��ʾ�汾��Ϣ
//  2,����"������"ʱ���ִ���TimeWait״̬������.
//  3,������"������",Connect������3000���ҵĶ˿�����
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

//�Զ���ѡ��  -------------------------------------------------------------------------------------------
const WORD AVERAGE_CONNECTIONS = 600;	//ƽ��������(���ֵ�ͳ��������й�)
#define SEND_BUF_DISABLE		//���÷��ͻ���,�����ڼ���memcpy()����,���ǻ�ʹ�����ٶȱ���.
					//�����������ʱ���û�������������.

#define LOG_ENABLE			//������־����
//#define LOG_LEVEL2			//��¼������Ϣ(����ʧ��,����ʧ�ܵȴ�����Ϣ)
#define LOG_LEVEL1			//��¼���ͺͽ��յ�����

//#define ENABLE_KEEPALIVE		//�����������.����ʱ��΢Ӱ��һ��Ч��
const DWORD KEEPALIVE_TIME  = 60*1000;	//�������ʱ��
const DWORD CLOSE_DELAY	= 1200;		//������CloseSock()�Ժ�,�ȴ�1200ms�ٵ��������Ĺرպ���

#define DEBUG_IOCP			//��һ���Ѿ�OnClose()��sock����Send,CloseSock��������_ASSERTE
#define DEBUG_ADO			//��ȡ������ʧ��ʱ����_ASSERTE.ע��:��ѯʧ�ܲ������_ASSERTE

//#define ENABLE_UNLIMIT_PORT		//����������.CLConnect()��������3000ʱʹ��
//------------------------------------------------------------------------------------------------------

using namespace std;

#pragma comment (lib,"ws2_32.lib")

const WORD DEFAULT_BUFFER_SIZE = 4096;
const WORD UNLIMIT_PORT_START = 13000;	//����������ʼ�˿�
const WORD UNLIMIT_PORT_END = 50000;	//����������߶˿�	

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
//	�ڴ�ش���ĺ���
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
//	�������Ҫ���ڼ��ز����Ľ�����
//
//------------------------------------------------------------------------------
class	cFixedMemoryPool;
class	cFixedMemoryPoolTracking 
{
public:
	static void * _pool_memcpy( const cFixedMemoryPool *, void * pDst, const void * pSrc, int size );	//	������������������ڴ�صĲ���
	static void * _pool_memset( const cFixedMemoryPool *, void * pDst, int value, int size );
	static void * _pool_memmove( const cFixedMemoryPool *, void * pDst, const void * pSrc, int size );
};

//------------------------------------------------------------------------------
//	�궨��
//	@ʹ�ú궨����Լ�¼�·���ˮ�ε�λ�ú���Դ���������ڿ��ٵĶ�λ�ڴ�й©�ľ���λ��
//	******************** ����ʹ�ú��������ڴ����ȷ���ڴ�صĽ�׳�� ************************
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
//	ˮ�εĶ���
//
//------------------------------------------------------------------------------
struct t_Blob 
{
	t_Blob	* m_pNext;

#ifdef _DEBUG_POOL
	t_Blob	* m_pPrior;	//	ָ����һˮ�ε�λ��
	void	* m_pPoolFather;//	ָ��Դ�ڴ�ص�ָ��,��������������ˮ�κ�ˮ���Ƿ���ƥ��
	LPCSTR	m_szFileName;	//	�����ˮ����Դ���ļ�
	DWORD	m_dwLine;	//	����������λ��
	DWORD	m_dwFreeFlag;	//	�ͷŵı�־λ,�����ظ��ͷ�
#endif
};

//------------------------------------------------------------------------------
//	�ص��ඨ��(�̶�����)
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
	virtual	int	Release	();		//�ͷ�

#ifdef _DEBUG_POOL
	virtual	void *	Alloc	( LPCSTR szFile = __FILE__, DWORD dwFileLine = __LINE__ );
#else
	virtual	void *	Alloc	();		//����
#endif

	virtual int Free( const void * ptr );	//�ͷ�

private:

	int	m_iBlobSize;		//ˮ�εĴ�С
	int	m_iFreeBlob;		//Ŀǰʣ���ˮ������
	int	m_iMaxBlob;		//���������ɵ�ˮ������
	int	m_Flag;			//���ⴴ���Ķ����Ƿ����free
	t_Blob	* m_pFreeBlob;		//�Ѿ����ͷŵ��ڴ�ڵ�.
	t_Blob	* m_pSelectedBlob;
	t_Blob	** m_pBlobPtr;		//ˮ��ָ�������λ��
	char	* m_pData;

	char	* m_pBeginAddress;
	char	* m_pEndAddress;

	CRITICAL_SECTION	m_sec;	

#ifdef _DEBUG_POOL
	t_Blob	* m_pLeakBlob;		//��������ˮ��ָ��
#endif
};

#endif _PB_MEMORYPOOL_H


//�������ݿ�������ADOConn

#if !defined(AFX_ADOCONN_H__AC448F02_AF26_45E4_9B2D_D7ECB8FFCFB9__INCLUDED_)
#define AFX_ADOCONN_H__AC448F02_AF26_45E4_9B2D_D7ECB8FFCFB9__INCLUDED_
#import "c:\Program Files\Common Files\System\ado\msado15.dll" no_namespace rename("EOF","adoEOF") rename("BOF","adoBOF")


class ADOConn  
{
public:
	ADOConn();
	virtual ~ADOConn();
	// ��ʼ�����������ݿ�
	bool  OpenDatabase(char *SqlConnectStr=NULL);
	// ִ�в�ѯ
	int GetRecord(_bstr_t bstrSQL);	// 0 - �ɹ�ִ�У�1 - ��¼Ϊ��, -1 ִ�����ݿ����ʧ��  

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

	// ִ��SQL��䣬Insert Update _variant_t
	int ExecuteSQL(_bstr_t bstrSQL); // 0 - �ɹ�ִ�У�1 - Ӱ��0����¼, -1 ִ�����ݿ����ʧ��  

	//��ȡ��¼����
	bool GetItem(const char *ItemName,int *Item);
	bool GetItem(const char *ItemName,DWORD *Item);
	bool GetItem(const char *ItemName,WORD *Item);
	bool GetItem(const char *ItemName,BYTE *Item);
	bool GetItem(const char *ItemName,bool *Item);
	bool GetItem(const char *ItemName,char *buf,int buflen);

private:
	//���һ��ָ��Connection�����ָ��:
	_ConnectionPtr m_pConnection;
	//���һ��ָ��Recordset�����ָ��:
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
	DWORD dwVar;		//dword����
	long  lVar;		//long����
	int   iVar;		//int����  		
	void  *pVar;		//ָ������
};


typedef struct _SOCKET_OBJ
{
	volatile SOCKET	s;		// Socket handle
	in_addr  addr;			// �ͻ��˵�ip��ַ
	volatile DWORD timealive;	// For keepalive check

	volatile BYTE flag_alive:1;	//KeepAlive��־,1-��Ч
	volatile BYTE flag_delay_close:1;//��ʱ�رձ�־,1-��Ч
	volatile BYTE flag_accept:1;	//OnAccept()��־,1-��ʾ�Ѿ�������OnAccept
	volatile BYTE flag_close:1;	//Close��־,1-sokcetδ�ر�

	volatile DWORD flag;		

	volatile long sending_count;	// ���ڵ���Send()�ĸ���

	BUFFER_OBJ *sendobj;
	BUFFER_OBJ *recvobj;

	CRITICAL_SECTION cs;		// Protect access to this structure

	PRIVATE_DATA myData;	

#ifdef DEBUG_IOCP			//������Ϣ
	BUFFER_OBJ *lastsend;		//���һ�εķ���obj
	int onclosed;			//�Ƿ������OnClose();
	SOCKET sockfd;			//socket��������ֵ
	int freeed;			//�Ƿ��Ѿ���free			
#endif
} SOCKET_OBJ;

class CIocpServer 
{
public :
	CIocpServer();
	virtual ~CIocpServer();

	// ��ʼ��
	// ConnectThread : ��Ϊ�ͻ�����������������ʱ���߳���
	// ListenThread : �������߳���
	// szConnect : ���ݿ������ַ���
	// ADOFlag : 01(������)-ֻ���������̵߳����ݿ����� , 
	//           10(������)- �����߳� ,11(������)-ͬʱ,0 - ��ʹ��ado
	int Init(int connect_thread,int listen_thread,int adoflag = 0,char *adostring = NULL);

	int StartServer(const char *local_ip,const char *local_port); //��ʼ����

	int Send(SOCKET_OBJ *sock,const void *buffer,int len);
	int CloseSock(SOCKET_OBJ *sock,bool close_delay = false); //�ر�����
	inline const char * GetAddress(SOCKET_OBJ *sock) //ȡ��ĳ���ͻ��˵�ip��ַ(�ַ���)
	{
		return inet_ntoa(sock->addr);
	}
	inline in_addr GetAddressEx(SOCKET_OBJ *sock) //ȡ��ĳ���ͻ��˵�ip��ַ(in_addr)
	{
		return sock->addr;
	}

	// int flag : ���ӱ�־.����ֵ����CLConnect()ʱ��flag����.
	// ��ͬʱ�����˶��������ʱ,���Ը���flag�жϵ�ǰsock��Ӧ�ķ���������.
	SOCKET_OBJ * CLConnect(const char *ip,const char *port,int flag); 	

	virtual void OnTimer();

	// SOCKET_OBJ *sock : ��ǰ���ӵĶ���,Send(),CloseSock()�ĵ�����Ҫ�ò���.����
	//                    ��sock->myData��������ʹ��,IOCP�ĵ��ò���ı���������.
	//                    �������û�����������֤ͨ��ʱ,��sock-myData���û���Ϣ��.
	//                    Ȼ�����sock���ٶ�λ�û���Ϣ.
	//
	// const char *buf : ���ջ���
	// int buflen : ���ջ�������ݳ���
	// ADOConn *pAdo : ÿ���̶߳�Ӧ�����ݿ����,Init()������������������ʱ��Ч,
	//                 ���򷵻�NULL.
	//
	// ����ֵ: ���ش���������ݳ���(OnClose(),CLOnClose()����0).
	//         ����: ��һ��OnRead()���յ���2.5��ָ��,������2��ָ���Ժ�,����2��ָ��
	//         ��buf����,����0.5��ָ�����һ�ε�OnRead()����.
	virtual int OnAccept(SOCKET_OBJ *sock,const char *buf,int buflen,ADOConn *pAdo); 
	virtual int OnRead(SOCKET_OBJ *sock,const char *buf,int buflen,ADOConn *pAdo); 
	virtual int OnClose(SOCKET_OBJ *sock,ADOConn *pAdo);  //���Է�������ֵ

	virtual int CLOnConnect(SOCKET_OBJ *sock,int flag);   //���Է�������ֵ
	virtual int CLOnRead(SOCKET_OBJ *sock,const char *buf,int buflen,int flag,ADOConn *pAdo);
	virtual int CLOnClose(SOCKET_OBJ *sock,int flag,ADOConn *pAdo); //���Է�������ֵ

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

	//��������һ�����������ǱȽ��鷳��������ֿ���д.
	friend unsigned int _stdcall CompletionThread(void * lpParam);  //�����߳�
	friend unsigned int _stdcall CLCompletionThread(void * lpParam);//�����߳�

	int PostRecv(SOCKET_OBJ *sock, BUFFER_OBJ *recvobj);
	int PostSend(SOCKET_OBJ *sock, BUFFER_OBJ *sendobj);
	int PostAccept(SOCKET_OBJ *sock, BUFFER_OBJ *acceptobj);

	inline void AddAlive(SOCKET_OBJ *sock,DWORD time); //��ӵ������б�
	inline void DeleteAlive(SOCKET_OBJ *sock);//���б���ɾ��
	void CheckAlive();//����Ƿ���ڳ�ʱ

	inline void AddClose(SOCKET_OBJ *sock,DWORD time);
	inline void DeleteClose(SOCKET_OBJ *sock);
	void CheckClose();//���رճ�ʱ 

	map<SOCKET_OBJ *,DWORD> m_mapAlive;
	CRITICAL_SECTION secAlive;

	map<SOCKET_OBJ *,DWORD> m_mapClose;
	CRITICAL_SECTION secClose;
};


#endif

















