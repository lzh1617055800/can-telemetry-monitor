#include "HttpRequest.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace std;
namespace{
    void require(bool condition,const string& message)
    {
        if(!condition)
        {
            cerr<<"[FAIL]"<<message<<endl;
            exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    HttpRequest request;
    const string get_request =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    require(request.parse(get_request),
            "valid GET request should parse");
    require(request.getMethod() == Method::GET,
            "method should be GET");
    require(request.getVersion() == Version::HTTP_11,
            "version should be HTTP/1.1");
    require(request.getPath() == "/health",
            "path should be /health");
    require(request.getHeader("Host") == "localhost",
            "Host header should be parsed");
    HttpRequest unsupported_version;
    require(
        !unsupported_version.parse(
            "GET / HTTP/9.9\r\n"
            "Host: localhost\r\n"
            "\r\n"
        ),
        "unsupported HTTP version should be rejected"
    );
    HttpRequest extra_token;
    require(!extra_token.parse("GET / HTTP/1.1 extra\r\n"
                                "Host:localhost\r\n"
                                "\r\n"),"request line with extra token should be rejected");
    cout << "http_request_test: PASS\n";
    return EXIT_SUCCESS;
}