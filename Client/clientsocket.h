#ifndef CLIENTSOCKET_H
#define CLIENTSOCKET_H
#include <stdio.h>
#include <iostream>
#include <winsock2.h>

#pragma comment(lib,"ws2_32.lib")



using std::string;
using std::cout;
using std::endl;

#define MAX_BUFFER_LEN  8 * 1024
#define DEFAULT_PORT    12345
#define DEFAULT_IP      "127.0.0.1"

#define MAX_THREAD_NUM  20
#define DEFAULT_MSG     "HELLO\n"


class Client;

typedef struct _threadSendWorker
{
    Client  *pClent;
    SOCKET  socket;

    int     nThreadNo;
    char    szBuffer[MAX_BUFFER_LEN];

} ThSendWorker, *LpThSendWorker;


typedef struct _threadConnectionWorker
{
    Client  *pClent;
} ThConnectionWorker, *LpThConnectionWorker;


class Client
{

public:
    Client();
    ~Client();

public:
    bool loadSockLib();
    void unLoadSockLib();

    bool start();
    void stop();

    string getLocalIp();

    void setIp(const string& ip) {m_serverIp = ip;}
    void setPort(const int port) {m_port = port;}

    void setThreadNums(const int num) {m_thNums = num;}
    void setMsg(const string& msg) {m_msg = msg;}


    string displayMsg(const char* fmt, ...);

private:
    bool establishConenect();
    bool connectToServer(SOCKET *s, const string& ip, const int port);

    void cleanUp();

    static DWORD WINAPI doConnectThread(LPVOID para);
    static DWORD WINAPI doSendThread(LPVOID para);


private:
    int m_port;
    int m_thNums;

    string m_serverIp, m_localIp;
    string m_msg;

    HANDLE* m_phWorkerThreads;
    HANDLE  m_hConnectionThread;
    HANDLE  m_shutDownEvent;

    ThSendWorker* m_pParamsWorker;

};
#endif // CLIENTSOCKET_H
