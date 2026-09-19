#include "Logger.h"
#include <iostream>
#include <ctime>
#include <chrono>
using namespace std;

Logger::Logger()
{
    level_ = LogLevel::INFO;
    file_.open("server.log",ios::app);
    if(!file_.is_open())
    {
        cerr<<"打开日志文件失败"<<endl;
    }
}

Logger::~Logger()
{
    file_.close();
}

Logger& Logger::getInstance()
{
    static Logger logger;
    return logger;
}

void Logger::setLevel(LogLevel level)
{
    level_ = level;
}

void Logger::log(LogLevel level,const string& message,const char* file,int line)
{
    if(level < level_)
    {
        return;
    }
    string filename = file;
    size_t pos = filename.rfind('/');
    if(pos != string::npos)
    {
        filename = filename.substr(pos+1);
    }
    string log_str = "[" + getCurrentTime() + "]"
                    +"[" + levelToString(level) + "]"
                    +"[" + filename + ":" + to_string(line) +"]"
                    + message;

    cout<<log_str<<endl;
    file_<<log_str<<endl;
    file_.flush();
    if(level == LogLevel::ERROR)
    {
        cerr<<log_str<<endl;
    }
}

string Logger::levelToString(LogLevel level)
{
    string lel;
    if(level == LogLevel::DEBUG)
    {
        lel = "DEBUG";
    }
    else if(level == LogLevel::ERROR)
    {
        lel = "ERROR";
    }
    else if(level == LogLevel::INFO)
    {
        lel = "INFO";
    }
    else if(level == LogLevel::WARN)
    {
        lel = "WARN";
    }
    return lel;
}

string Logger::getCurrentTime()
{
    char buf[64];
    time_t now = time(nullptr);
    tm *local = localtime(&now);
    strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S", local);
    return string(buf);
}