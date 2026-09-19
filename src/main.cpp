#include "Server.h"
#include "EventLoop.h"
#include "Logger.h"
#include "CanRuntime.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
using namespace std;
int main()
{
    signal(SIGINT,EventLoop::signalHandler);
    signal(SIGTERM,EventLoop::signalHandler);
    const char* can_interface = std::getenv("CAN_INTERFACE");
    CanRuntime can_runtime(
        can_interface == nullptr
            ? "vcan0"
            : can_interface);

    std::string can_error;
    if(!can_runtime.start(&can_error))
    {
        LOG_ERROR("CAN runtime initialization failed: " + can_error);
        return EXIT_FAILURE;
    }

    Server server(8080, "0.0.0.0", &can_runtime);
    if(!server.init())
    {
        LOG_ERROR("服务器初始化失败");
        can_runtime.stop();
        return -1;
    }
    server.start();
    can_runtime.stop();
    return 0;
}
