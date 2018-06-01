#include "clientsocket.h"
#include <thread>


#pragma comment(lib, "ws2_32.lib")

#define RELEASE(X) {if(X != NULL) {delete X; X = NULL;}}
#define RELEASE_HANDLE(X) {if (X != NULL && X != INVALID_HANDLE_VALUE) {CloseHandle(X); X = NULL;}}


DWORD WINAPI Client::doSendThread(LPVOID para)
{
    ThSendWorker* worker = static_cast<ThSendWorker*>(para);
    Client *cl = worker->pClent;

    char szTmp[MAX_BUFFER_LEN], szBuffer[MAX_BUFFER_LEN];
    memset(szTmp, 0, sizeof(szTmp));
    memset(szBuffer, 0, sizeof(szBuffer));

    int nByteSend = 0, nByteRecv = 0;

    sprintf(szTmp, "firt msg: %s", worker->szBuffer);
    cout << "szTmp: "<<szTmp<<endl;

    nByteSend = send(worker->socket, szTmp, strlen(szTmp), 0);
    if (nByteSend == SOCKET_ERROR) {
        cout << "send firt data failed, error:"<<WSAGetLastError()<<endl;
        return -1;
    }

    //std::this_thread::sleep_for(std::chrono::milliseconds(1));

    memset(szTmp, 0, sizeof(szTmp));
    sprintf(szTmp, "number tow msg:%s", worker->szBuffer);
    nByteSend = send(worker->socket, szTmp, strlen(szTmp), 0);
    if (nByteSend == SOCKET_ERROR) {
        cout << "send nunber tow data failed, error:"<<WSAGetLastError()<<endl;
        return -1;
    }

    return 0;
}

DWORD WINAPI Client::doConnectThread(LPVOID para)
{
    ThConnectionWorker *worker = (ThConnectionWorker*)para;
    Client *cl = worker->pClent;

    cout << "start connecting to server!"<<endl;
    cl->establishConenect();

    cout << "thread end!"<<endl;

    RELEASE(worker);

    return 0;
}

Client::Client()
    : m_serverIp(DEFAULT_IP),
      m_localIp(DEFAULT_IP),
      m_port(DEFAULT_PORT),
      m_thNums(MAX_THREAD_NUM),
      m_msg(DEFAULT_MSG),
      m_phWorkerThreads(NULL),
      m_hConnectionThread(NULL),
      m_shutDownEvent(NULL),
      m_pParamsWorker(NULL)
{

}

Client::~Client()
{
    stop();
}

bool Client::loadSockLib()
{
    bool flag = false;
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (NO_ERROR == ret) {
        flag = true;
    }
    return flag;
}

void Client::unLoadSockLib()
{
    WSACleanup();
}

bool Client::start()
{
    bool flag = true;

    m_shutDownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    DWORD nThreadId;
    ThConnectionWorker *_connectWorker = new ThConnectionWorker();
    _connectWorker->pClent = this;

    m_hConnectionThread = ::CreateThread(NULL, 0, doConnectThread, (LPVOID)_connectWorker, 0, &nThreadId);

    return flag;
}

void Client::stop()
{
    if (m_shutDownEvent == NULL) return;
    SetEvent(m_shutDownEvent);

    WaitForSingleObject(m_hConnectionThread, INFINITE);
    for (int i = 0; i < m_thNums; ++i) {
        closesocket(m_pParamsWorker[i].socket);
    }
    WaitForMultipleObjects(m_thNums, m_phWorkerThreads, TRUE, INFINITE);
    cleanUp();
    cout << "done end"<<endl;
}

bool Client::establishConenect()
{
    DWORD nThreadId;
    m_phWorkerThreads = new HANDLE[m_thNums];

    m_pParamsWorker = new ThSendWorker[m_thNums];

    for (int i = 0; i < m_thNums; ++i) {

        if (WAIT_OBJECT_0 == WaitForSingleObject(m_shutDownEvent, 0)) {
            cout << "receive user interupt cmd\n" <<endl;
            return true;
        }

        if (!this->connectToServer(&m_pParamsWorker[i].socket, m_serverIp, m_port)) {
            cout << displayMsg("connect to:%s, port:%d faild\n", m_serverIp.c_str(), m_port).c_str() <<endl;
            cleanUp();
            return false;
        }

        m_pParamsWorker[i].nThreadNo = i+1;
        sprintf(m_pParamsWorker[i].szBuffer, "%d thread send data: %s",
                m_pParamsWorker[i].nThreadNo, m_msg.c_str());


        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        m_pParamsWorker[i].pClent = this;
        m_phWorkerThreads[i] = CreateThread(NULL, 0, doSendThread,
                                            (LPVOID)&m_pParamsWorker[i], 0, &nThreadId);
    }
    //TO DO
    return true;
}

bool Client::connectToServer(SOCKET *s, const std::string &ip, const int port)
{
    SOCKADDR_IN serAddr;
    struct hostent *ent;
    cout << ip.c_str()<<endl;

    *s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (INVALID_SOCKET == *s) {
        cout << "failed socket, error:" << WSAGetLastError() <<endl;
        return false;
    }

    ent = gethostbyname(ip.c_str());
    if (NULL == ent) {
        cout << "server address is invalid\n" <<endl;
        closesocket(*s);
        return false;
    }

    ZeroMemory((char*)&serAddr, sizeof(SOCKADDR_IN));
    serAddr.sin_family = AF_INET;
    CopyMemory((char*)&serAddr.sin_addr.S_un.S_addr, (char*)ent->h_addr, ent->h_length);

    serAddr.sin_port = htons(port);

    if (SOCKET_ERROR == connect(*s, (SOCKADDR*)&serAddr, sizeof(serAddr))) {
        cout << "错误：连接至服务器失败！\n" <<endl;
        closesocket(*s);
        return false;
    }

    return true;

}


void Client::cleanUp()
{
    if (m_shutDownEvent == NULL) return;
    RELEASE(m_phWorkerThreads);
    RELEASE_HANDLE(m_hConnectionThread);
    RELEASE(m_pParamsWorker);
    RELEASE_HANDLE(m_shutDownEvent);
}

string Client::getLocalIp()
{
    char hostname[MAX_PATH];
    ::gethostname(hostname, sizeof(hostname));

    struct hostent *ent = gethostbyname(hostname);
    if (NULL == ent) {
        m_localIp = DEFAULT_IP;
        return m_localIp;
    }

    char *lpAddr = ent->h_addr_list[0];
    struct in_addr in_ad;
    memmove(&in_ad, lpAddr, 4);

    m_localIp = string(inet_ntoa(in_ad));
    cout << "hostname: "<<m_localIp.c_str()<<endl;

    return m_localIp;
}


string Client::displayMsg(const char *fmt, ...)
{
   char buffer[1024] = {0};
   va_list a_list;
   va_start(a_list, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, a_list);
   va_end(a_list);

   string str(buffer);
   return str;
}
