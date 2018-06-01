#include "iocpserver.h"


DWORD WINAPI IOCPServer::workerThread(LPVOID lpParam)
{

    THREAD_PARAMS_WORKER* worker = static_cast<THREAD_PARAMS_WORKER*>(lpParam);
    IOCPServer* iocpServer = static_cast<IOCPServer*>(worker->server);
    int nThreadNo = worker->nThreadNo;

    iocpServer->showMessage("worker thread startup, threadId is %d", nThreadNo);

    OVERLAPPED *pOverLapped = NULL;
    PER_SOCKET_CONTEXT* sp_context = NULL;
    DWORD nBytesTransfered = 0;

    while (WAIT_OBJECT_0 != WaitForSingleObject(iocpServer->m_hShutdownEvent, 0)) {

        BOOL retReturn = GetQueuedCompletionStatus(iocpServer->m_hIocp, &nBytesTransfered,
                                                   (PULONG_PTR)&sp_context, &pOverLapped, INFINITE);

        if (EXIT_CODE == (DWORD)sp_context) {
            break;
        }

        if (!retReturn) {

            DWORD err = GetLastError();
            if (!iocpServer->handleError(sp_context, err)) {
                break;
            }

            continue;
        } else {

            //读取传入的参数
            PER_IO_CONTEXT* ioContext = CONTAINING_RECORD(pOverLapped, PER_IO_CONTEXT, m_overlapped);

            if (0 == nBytesTransfered && (SEND_POSTED == ioContext->m_type || RECV_POSTED == ioContext->m_type)) {
                iocpServer->showMessage("client %s:%d quit\n", inet_ntoa(sp_context->m_clientAddr.sin_addr),
                                        ntohs(sp_context->m_clientAddr.sin_port));
                iocpServer->removeSocketContext(sp_context);
                continue;
            } else {

                switch (ioContext->m_type) {
                case ACCEPT_POSTED:
                    iocpServer->doAccept(sp_context, ioContext);
                    break;

                case SEND_POSTED:

                    break;

                case RECV_POSTED:
                    iocpServer->doRecv(sp_context, ioContext);
                    break;
                default:
                    break;
                }
            }
        }
    }

    return 0;
}

IOCPServer::IOCPServer()
    : m_port(DEFAULT_PORT), m_ip(DEFAULT_IP),
      m_hIocp(NULL), m_threadNums(0),
      m_hShutdownEvent(NULL), m_phWorkerThreads(NULL),
      m_lpfnAcceptEx(NULL), m_lpfnAcceptSockAddrs(NULL)
{
    loadSocketLib();
}

IOCPServer::~IOCPServer()
{
    stopServer();
}

bool IOCPServer::startServer()
{
    InitializeCriticalSection(&m_cSection);

    m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!initIocp()) {
        showMessage("initIocp failed\n");
        return false;
    }

    showMessage("initIocp success\n");

    if (!initListenSocket()) {
        showMessage("init listen socket failed\n");
        unInit();
        return false;
    }

    showMessage("init listen socket success!\n");

    return true;
}

void IOCPServer::stopServer()
{
    if (m_spListenSocketContext != NULL && m_spListenSocketContext->m_socket != INVALID_SOCKET) {

        SetEvent(m_hShutdownEvent);

        for (auto i = 0; i < m_threadNums; ++i) {
            PostQueuedCompletionStatus(m_hIocp, 0, (DWORD)EXIT_CODE, NULL);
        }
        WaitForMultipleObjects(m_threadNums, m_phWorkerThreads, TRUE, INFINITE);

        clearSocketContexts();
        unInit();
        showMessage("stop server!\n");
    }
}

bool IOCPServer::initIocp()
{
    m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (NULL == m_hIocp) {
        showMessage("create iocp failed, error code:%d\n", WSAGetLastError());
        return false;
    }

    m_threadNums = WORKERS_PER_PROCESSOR * getLocalProcessors();
    m_phWorkerThreads = new HANDLE[m_threadNums];

    DWORD nthreadID;
    for (auto i = 0; i < m_threadNums; ++i) {
        THREAD_PARAMS_WORKER* lpParam = new THREAD_PARAMS_WORKER;
        lpParam->server = this;
        lpParam->nThreadNo = i + 1;
        m_phWorkerThreads[i] = ::CreateThread(NULL, NULL, workerThread, (LPVOID)lpParam, 0, &nthreadID);
    }
    showMessage("created %d count thread\n", m_threadNums);
    return true;
}

bool IOCPServer::initListenSocket()
{
    GUID _guid_acceptEx = WSAID_ACCEPTEX;
    GUID _guid_acceptSockAddr = WSAID_GETACCEPTEXSOCKADDRS;

    SOCKADDR_IN _serAddr;

    m_spListenSocketContext = new PER_SOCKET_CONTEXT; //std::make_unique<PER_SOCKET_CONTEXT>();

    m_spListenSocketContext->m_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (INVALID_SOCKET == m_spListenSocketContext->m_socket) {
        showMessage("create listen socket failed, error code:%d", WSAGetLastError());
        return false;
    }

    if (NULL == CreateIoCompletionPort((HANDLE)m_spListenSocketContext->m_socket, m_hIocp,
                                       (ULONG_PTR)&m_spListenSocketContext, 0)) {
        showMessage("bind listen socket to iocp failed, error code:%d\n",WSAGetLastError());
        RELEASE_SOCKET(m_spListenSocketContext->m_socket);
        return false;
    }

    memset((char*)&_serAddr, 0, sizeof(_serAddr));
    _serAddr.sin_family = AF_INET;
    _serAddr.sin_addr.S_un.S_addr = inet_addr(m_ip.c_str());
    _serAddr.sin_port = htons(m_port);

    if (SOCKET_ERROR == ::bind(m_spListenSocketContext->m_socket, (SOCKADDR*)&_serAddr, sizeof(_serAddr))) {
        return false;
    }

    if (SOCKET_ERROR == ::listen(m_spListenSocketContext->m_socket, SOMAXCONN)) {
        showMessage("listen function error");
        return false;
    }

    DWORD dwBytes = 0;
    if (SOCKET_ERROR == WSAIoctl(m_spListenSocketContext->m_socket,
                                 SIO_GET_EXTENSION_FUNCTION_POINTER,
                                 &_guid_acceptEx, sizeof(_guid_acceptEx),
                                 &m_lpfnAcceptEx, sizeof(m_lpfnAcceptEx),
                                 &dwBytes, NULL, NULL))
    {
        showMessage("get acceptEx function pointer address failed\n");
        unInit();
        return false;
    }

    if (SOCKET_ERROR == WSAIoctl(m_spListenSocketContext->m_socket,
                                 SIO_GET_EXTENSION_FUNCTION_POINTER,
                                 &_guid_acceptSockAddr, sizeof(_guid_acceptSockAddr),
                                 &m_lpfnAcceptSockAddrs, sizeof(m_lpfnAcceptSockAddrs),
                                 &dwBytes, NULL, NULL))
    {
        showMessage("get acceptSockADDRS function pointer address failed\n");
        unInit();
        return false;
    }

    for (auto i = 0; i < MAX_POST_ACCEPT_NUM; ++i) {
        PER_IO_CONTEXT* spIoContext = m_spListenSocketContext->addNewIoContext();
        if (!postAccept(spIoContext)) {
            m_spListenSocketContext->removeIoContext(spIoContext);
            return false;
        }
    }

    showMessage("post % count accept success!\n", MAX_POST_ACCEPT_NUM);

    return true;
}

bool IOCPServer::loadSocketLib()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR) {
        showMessage("\nstartup wsa failed, error:%s\n", WSAGetLastError());
        return false;
    }
    return true;
}

void IOCPServer::unLoadSocketLib()
{
    WSACleanup();
}

void IOCPServer::unInit()
{
    DeleteCriticalSection(&m_cSection);
    RELEASE_HANDLE(m_hShutdownEvent);

    for (auto i = 0; i < m_threadNums; ++i) {
        RELEASE_HANDLE(m_phWorkerThreads[i]);
    }
    RELEASE(m_phWorkerThreads);
    RELEASE_HANDLE(m_hIocp);

    RELEASE(m_spListenSocketContext);

    showMessage("release resource done!\n");
}

string IOCPServer::getLocalIp()
{
    char hostname[MAX_PATH] = {0};
    gethostname(hostname, sizeof(hostname));

    struct hostent FAR* ent = gethostbyname(hostname);

    if (ent == NULL) {
        setIp(DEFAULT_IP);
        return m_ip;
    }

    LPSTR lpStrIp = ent->h_addr_list[0];
    struct in_addr i_addr;
    memmove(&i_addr, lpStrIp, 4);

    string tmp = string(inet_ntoa(i_addr));
    setIp(tmp);
    cout << "ip: " <<m_ip.c_str() <<endl;
    return tmp;
}

void IOCPServer::setIp(const string &ip)
{
    m_ip = ip;
}

void IOCPServer::setPort(const int port)
{
    m_port = port;
}

bool IOCPServer::postAccept(PER_IO_CONTEXT *ioContext)
{
    if (m_spListenSocketContext->m_socket == INVALID_SOCKET) return false;

    DWORD dwBytes = 0;
    ioContext->m_type = ACCEPT_POSTED;
    WSABUF *p_wsaBuf = &ioContext->m_wsabuf;
    OVERLAPPED* p_ol = &ioContext->m_overlapped;

    ioContext->m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (INVALID_SOCKET == ioContext->m_socket) {
        showMessage("crete socket failed, error code:%d\n", WSAGetLastError());
        return false;
    }

    if (FALSE == m_lpfnAcceptEx(m_spListenSocketContext->m_socket,
                                ioContext->m_socket, p_wsaBuf->buf,
                                p_wsaBuf->len- ((sizeof(SOCKADDR_IN) + 16) * 2),
                                (sizeof(SOCKADDR_IN) + 16), (sizeof(SOCKADDR_IN) + 16),
                                &dwBytes, p_ol))
    {
        if (WSA_IO_PENDING != WSAGetLastError()) {
            showMessage("post accept failed, error code:%d\n", WSAGetLastError());
            return false;
        }
    }

    return true;
}

bool IOCPServer::doAccept(PER_SOCKET_CONTEXT *socketContext,
                          PER_IO_CONTEXT *ioContext)
{
    SOCKADDR_IN* _clientAddr = NULL;
    SOCKADDR_IN* _localAddr  = NULL;

    int remoteLen = sizeof(SOCKADDR_IN), LocalLen = sizeof(SOCKADDR_IN);

    m_lpfnAcceptSockAddrs(ioContext->m_wsabuf.buf,
                          ioContext->m_wsabuf.len - ((sizeof(SOCKADDR_IN) + 16) * 2),
                          sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                          (LPSOCKADDR*)&_localAddr, &LocalLen, (LPSOCKADDR*)&_clientAddr, &remoteLen);

    showMessage("client %s:%d join, msg:%s\n", inet_ntoa(_clientAddr->sin_addr),
                ntohs(_clientAddr->sin_port), ioContext->m_wsabuf.buf);



    PER_SOCKET_CONTEXT* _tp_socketContext = new PER_SOCKET_CONTEXT;
    _tp_socketContext->m_socket = ioContext->m_socket;

    memcpy(&(_tp_socketContext->m_clientAddr), _clientAddr, sizeof(SOCKADDR_IN));

    if (!bindSocketContextToIocp(_tp_socketContext)) {
        RELEASE(_tp_socketContext);
        return false;
    }

    showMessage("now addr %s:%d \n", inet_ntoa(_tp_socketContext->m_clientAddr.sin_addr),
                ntohs(_tp_socketContext->m_clientAddr.sin_port));

    PER_IO_CONTEXT* _sp_ioContext = _tp_socketContext->addNewIoContext();
    _sp_ioContext->m_type = RECV_POSTED;
    _sp_ioContext->m_socket = _tp_socketContext->m_socket;

    if (!postRecv(_sp_ioContext)) {
        _tp_socketContext->removeIoContext(_sp_ioContext);
        return false;
    }

    addToSocketContexts(_tp_socketContext);

    ioContext->resetBuffer();

    return postAccept(ioContext);
}

bool IOCPServer::postRecv(PER_IO_CONTEXT *ioContext)
{
    DWORD dwFlags = 0, dwBytes = 0;

    WSABUF *p_wbuf = &ioContext->m_wsabuf;
    OVERLAPPED *p_ol = &ioContext->m_overlapped;

    ioContext->m_type = RECV_POSTED;
    ioContext->resetBuffer();

    int nBytesRecv = WSARecv(ioContext->m_socket, p_wbuf, 1, &dwBytes, &dwFlags, p_ol, NULL);
    if (SOCKET_ERROR == nBytesRecv && WSA_IO_PENDING != WSAGetLastError()) {
        showMessage("post receve post failed\n");
        return false;
    }
    return true;
}

bool IOCPServer::doRecv(PER_SOCKET_CONTEXT *socketContext,
                        PER_IO_CONTEXT *ioContext)
{
    SOCKADDR_IN* _clientAddr = &socketContext->m_clientAddr;
    showMessage("receve %s:%d, msg:%s\n", inet_ntoa(_clientAddr->sin_addr),
                ntohs(_clientAddr->sin_port), ioContext->m_wsabuf.buf);

    return postRecv(ioContext);
}

void IOCPServer::addToSocketContexts(PER_SOCKET_CONTEXT *socketContext)
{
    EnterCriticalSection(&m_cSection);
    m_spSocketContexts.push_back(socketContext);
    LeaveCriticalSection(&m_cSection);
}

void IOCPServer::removeSocketContext(PER_SOCKET_CONTEXT *socketContext)
{
    EnterCriticalSection(&m_cSection);
    for (auto i = m_spSocketContexts.begin(); i != m_spSocketContexts.end(); ++i) {
        if (*i == socketContext)  {
            delete *i;
            *i = NULL;
            m_spSocketContexts.erase(i);
            break;
        }
    }
    LeaveCriticalSection(&m_cSection);
}

void IOCPServer::clearSocketContexts()
{
    for (auto i = m_spSocketContexts.begin(); i != m_spSocketContexts.end(); ++i) {
        delete *i;
        *i = NULL;
    }
    vector<PER_SOCKET_CONTEXT*>().swap(m_spSocketContexts);
}

bool IOCPServer::bindSocketContextToIocp(PER_SOCKET_CONTEXT *socketContext)
{
    HANDLE _h = CreateIoCompletionPort((HANDLE)socketContext->m_socket, m_hIocp, (ULONG_PTR)socketContext, 0);
    if (NULL == _h) {
        showMessage("execute bindSocketContextToIocp error!\n");
        return false;
    }
    return true;
}

bool IOCPServer::handleError(PER_SOCKET_CONTEXT *socketContext, const DWORD &dwErr)
{
    if (dwErr == WAIT_TIMEOUT) {

        if (!isSocketAlive(socketContext->m_socket)) {
            showMessage("discover client quit\n");
            removeSocketContext(socketContext);
        } else {
            showMessage("time out\n");
        }
        return true;

    } else if (dwErr == ERROR_NETNAME_DELETED) {
        showMessage("discover client quit\n");
        removeSocketContext(socketContext);
        return true;
    } else {
        showMessage("iocp appear unknown error!\n");
        return false;
    }
}

void IOCPServer::showMessage(const char *fmt, ...) const
{
    char buffer[1024] = {0};

    va_list a_list;
    va_start(a_list, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, a_list);
    va_end(a_list);

    cout << buffer << endl;
}

int IOCPServer::getLocalProcessors()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
}

bool IOCPServer::isSocketAlive(SOCKET s)
{
    int nByteSend = ::send(s, "", 0, 0);
    if (nByteSend == -1) return false;
    return true;
}
