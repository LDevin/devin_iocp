#ifndef IOCPSERVER_H
#define IOCPSERVER_H
#include <stdio.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <memory>
#include <WinSock2.h>
#include <MSWSock.h>

#pragma comment(lib, "ws2_32.lib")


using std::cout;
using std::endl;
using std::vector;
using std::string;


#define DEFAULT_IP      "192.168.10.177"
#define DEFAULT_PORT    12345

#define MAX_BUFFER_LEN  8*1024

//每个处理器产生2个线程
#define WORKERS_PER_PROCESSOR 2
#define MAX_POST_ACCEPT_NUM 10

#define EXIT_CODE NULL

#define RELEASE(X) {if (X != NULL) {delete X; X = NULL;}}

#define RELEASE_HANDLE(X) {if (X != NULL && X != INVALID_HANDLE_VALUE) \
{CloseHandle(X); X = INVALID_HANDLE_VALUE;}}

#define RELEASE_SOCKET(X) {if (X != INVALID_SOCKET) {closesocket(X); X = INVALID_SOCKET;}}


typedef enum _OPE_TYPE
{
    ACCEPT_POSTED,
    SEND_POSTED,
    RECV_POSTED,
    NULL_POSTED
} OPE_TYPE;


typedef struct _PER_IO_CONTEXT
{
    OVERLAPPED m_overlapped;
    SOCKET     m_socket;
    WSABUF     m_wsabuf;
    char       m_szbuf[MAX_BUFFER_LEN];
    OPE_TYPE   m_type;

    _PER_IO_CONTEXT()
    {
        memset(&m_overlapped, 0, sizeof(m_overlapped));
        memset(m_szbuf, 0, MAX_BUFFER_LEN);

        m_wsabuf.buf = m_szbuf;
        m_wsabuf.len = MAX_BUFFER_LEN;
        m_socket = INVALID_SOCKET;
        m_type=  NULL_POSTED;
    }
    ~_PER_IO_CONTEXT()
    {
        RELEASE_SOCKET(m_socket);
    }

    void resetBuffer()
    {
        memset(m_szbuf, 0, MAX_BUFFER_LEN);
    }
} PER_IO_CONTEXT, *PPER_IO_CONTEXT;

//采用智能指针
using SP_PER_IO_CONTEXT     = std::unique_ptr<PER_IO_CONTEXT> ;

typedef struct _PER_SOCKET_CONTEXT
{
    SOCKET m_socket;
    SOCKADDR_IN m_clientAddr;

    //采用智能指针
    vector<PER_IO_CONTEXT*> m_io_context_vec;

    _PER_SOCKET_CONTEXT()
    {
        m_socket = INVALID_SOCKET;
        memset(&m_clientAddr, 0, sizeof(m_clientAddr));
        m_io_context_vec.reserve(50000);
    }

    ~_PER_SOCKET_CONTEXT()
    {
       RELEASE_SOCKET(m_socket);

        //清空vector
       // vector<SP_PER_IO_CONTEXT>().swap(m_io_context_vec);
       for (auto i = m_io_context_vec.begin(); i != m_io_context_vec.end(); ++i) {
           delete *i;
           *i = NULL;
       }
       vector<PER_IO_CONTEXT*>().swap(m_io_context_vec);
    }

    PER_IO_CONTEXT* addNewIoContext() noexcept
    {
        //SP_PER_IO_CONTEXT sp = std::make_unique<PER_IO_CONTEXT>();
        PER_IO_CONTEXT* sp = new PER_IO_CONTEXT;
        m_io_context_vec.push_back(sp);
        return sp;
    }

    void removeIoContext(PER_IO_CONTEXT* pIoContext) noexcept
    {
//        m_io_context_vec.erase(std::remove(m_io_context_vec.begin(), m_io_context_vec.end(), pIoContext),
//                               m_io_context_vec.end());

//        vector<PER_IO_CONTEXT*> (m_io_context_vec).swap(m_io_context_vec);

        for (auto i = m_io_context_vec.begin(); i != m_io_context_vec.end(); ++i) {
            if (pIoContext == *i) {
                delete *i;
                *i = NULL;
                m_io_context_vec.erase(i);
                break;
            }
        }
    }

} PER_SOCKET_CONTEXT, *PPER_SOCKET_CONTEXT;

using SP_PER_SOCKET_CONTEXT = std::unique_ptr<PER_SOCKET_CONTEXT>;

class IOCPServer;
typedef struct _TAG_THREAD_PARAMS_WORKER
{
    IOCPServer* server;
    int nThreadNo;
} THREAD_PARAMS_WORKER, *PTHREAD_PARAMS_WORKER;



class IOCPServer
{

public:
    IOCPServer();
    ~IOCPServer();

    void showMessage(const char* fmt, ...) const;

public:
    bool startServer();
    void stopServer();

    bool loadSocketLib();
    void unLoadSocketLib();

    string getLocalIp();

    void setPort(const int port);
    void setIp(const string& ip);

protected:

    bool initIocp();
    bool initListenSocket();

    void unInit();

    bool postAccept(PER_IO_CONTEXT* ioContext);
    bool doAccept(PER_SOCKET_CONTEXT* socketContext,
                  PER_IO_CONTEXT* ioContext);

    bool postRecv(PER_IO_CONTEXT* ioContext);
    bool doRecv(PER_SOCKET_CONTEXT* socketContext,
                PER_IO_CONTEXT* ioContext);

    void addToSocketContexts(PER_SOCKET_CONTEXT* socketContext);
    void removeSocketContext(PER_SOCKET_CONTEXT* socketContext);

    void clearSocketContexts();

    bool bindSocketContextToIocp(PER_SOCKET_CONTEXT* socketContext);

    bool handleError(PER_SOCKET_CONTEXT* socketContext,
                     const DWORD& dwErr);

    //处理IOCP请求的线程
    static DWORD WINAPI workerThread(LPVOID lpParam);

private:
    int getLocalProcessors();
    bool isSocketAlive(SOCKET s);

private:
    int                 m_port;
    string              m_ip;

    HANDLE              m_hShutdownEvent;
    HANDLE              m_hIocp;

    HANDLE*             m_phWorkerThreads;

    int                 m_threadNums;

    CRITICAL_SECTION    m_cSection;

    vector<PER_SOCKET_CONTEXT*> m_spSocketContexts;

    PER_SOCKET_CONTEXT*         m_spListenSocketContext;

    LPFN_ACCEPTEX                 m_lpfnAcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS     m_lpfnAcceptSockAddrs;

};
#endif // IOCPSERVER_H
