#ifndef SERVER_H
#define SERVER_H
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "EventLoop.h"
using namespace std;
class CanRuntime;
class Server
{
public:
    Server(
        int port,
        const string& ip="0.0.0.0",
        CanRuntime* can_runtime=nullptr);
    ~Server();
    bool init();
    void start();

    static int getListenfd(){return listen_fd;}

private:
    int createListenfd();
    void setNonBlocking(int fd);
    int port_;
    string ip_;
    static int listen_fd;
    struct sockaddr_in server_addr_;
    EventLoop* event_loop;
    CanRuntime* can_runtime_;

};
#endif
