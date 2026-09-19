#ifndef LOGGER_H
#define LOGGER_H
#include <string>
#include <fstream>
using namespace std;
enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};
class Logger
{
public:
    static Logger& getInstance();
    void setLevel(LogLevel level);
    void log(LogLevel level,const string&message,const char* file,int line);
private:
    Logger();
    ~Logger();
    string levelToString(LogLevel level);
    string getCurrentTime();

    LogLevel level_;
    ofstream file_;
};
#define LOG_DEBUG(msg) Logger::getInstance().log(LogLevel::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg)  Logger::getInstance().log(LogLevel::INFO,  msg, __FILE__, __LINE__)
#define LOG_WARN(msg)  Logger::getInstance().log(LogLevel::WARN,  msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::getInstance().log(LogLevel::ERROR, msg, __FILE__, __LINE__)
#endif