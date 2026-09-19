#include "HttpResponse.h"
#include <iostream>
#include <string>
using namespace std;

const unordered_map<int, string> HttpResponse::status_messages_ = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {500, "Internal Server Error"},
    {503, "Service Unavailable"},
};

HttpResponse::HttpResponse()
    :status_code_(200),status_message_("OK"){}

void HttpResponse::setStatusCode(int code)
{
    status_code_ = code;
    auto it = status_messages_.find(code);
    if(it != status_messages_.end())
    {
        status_message_ = it->second;
    }
    else{
        status_message_ = "Unknown";
    }
}

void HttpResponse::setStatusCode(int code,const string& message)
{
    status_code_ = code;
    status_message_ = message;
}

void HttpResponse::setHeader(const string& key,const string& value)
{
    headers_[key] = value;
}

void HttpResponse::setBody(const string& body)
{
    body_ = body;
}

void HttpResponse::setBody(const char* data,size_t length)
{
    body_ = string(data,length);
}

string HttpResponse::getMimeType(const string& filepath)
{
    size_t pos = filepath.rfind('.');
    if(pos == string::npos)
    {
        return "application/octet-stream";
    }
    string ext = filepath.substr(pos+1);
    if (ext == "html" || ext == "htm")
    {
        return "text/html; charset=utf-8";
    }
    else if(ext == "css")
    {
        return "text/css";
    }
    else if(ext == "js")
    {
        return "application/javascript";
    }
    else if(ext == "json")
    {
        return "application/json";
    }
    else if(ext == "png")
    {
        return "image/png";
    }
    else if(ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }
    else if(ext == "gif")
    {
        return "image/gif";
    }
    else if(ext == "ico")
    {
        return "image/x-icon";
    }
    else if(ext == "svg")
    {
        return "image/svg+xml";
    }
    else if(ext == "txt")
    {
        return "text/plain";
    }
    else{
        return "application/octet-stream";
    }
}

string HttpResponse::serialize() const
{
    string result;
    result  = "HTTP/1.1 " + to_string(status_code_) + " " + status_message_ + "\r\n";
    for(auto&  [key,value]:headers_)
    {
        result += key + ": " + value + "\r\n";
    }
    if (headers_.find("Content-Length") == headers_.end()) {
        result += "Content-Length: " + to_string(body_.size()) + "\r\n";
    }

    // 4. 空行（分隔头部和正文）
    result += "\r\n";

    // 5. 响应体
    result += body_;

    return result;
}
