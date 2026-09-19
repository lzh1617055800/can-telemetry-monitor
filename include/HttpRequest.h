#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H
#include <string>
#include <unordered_map>
using namespace std;
enum class Method
{
    GET,POST,PUT,DELETE,UNKNOWN
};
enum class Version
{
    HTTP_10,HTTP_11,UNKNOWN
};
class HttpRequest
{
public:
    HttpRequest():method(Method::UNKNOWN),version(Version::UNKNOWN){};
    bool parse(const string& data);
    Method getMethod()const{return method;}
    Version getVersion()const{return version;}
    const string& getPath()const{return path;}
    const string& getHeader(const string& key)const;
    const string& getBody()const{return body;}
private:
    bool parseRequestLine(const string& line);
    bool parseHeader(const string& line);
    Method method;
    string path;
    Version version;
    string body;//请求体，用于解决POST
    unordered_map <string, string> headers;
};
#endif
