#include "Connection.h"
#include "EventLoop.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "CanRuntime.h"
#include "Logger.h"
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <map>
#include <optional>
#include <vector>
#include <unistd.h>

using namespace std;

namespace
{
string trim(const string& value)
{
    const string whitespace = " \t\r\n";
    size_t begin = value.find_first_not_of(whitespace);
    if(begin == string::npos)
    {
        return "";
    }
    size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

size_t extractContentLength(const string& request)
{
    size_t header_end = request.find("\r\n\r\n");
    if(header_end == string::npos)
    {
        return 0;
    }

    istringstream iss(request.substr(0, header_end));
    string line;

    if(!getline(iss, line))
    {
        return 0;
    }

    while(getline(iss, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if(line.empty())
        {
            break;
        }

        size_t colon = line.find(':');
        if(colon == string::npos)
        {
            continue;
        }

        string key = trim(line.substr(0, colon));
        string value = trim(line.substr(colon + 1));
        if(key == "Content-Length")
        {
            try
            {
                return static_cast<size_t>(stoul(value));
            }
            catch(...)
            {
                return 0;
            }
        }
    }

    return 0;
}

string pathWithoutQuery(const string& path)
{
    const size_t question_mark = path.find('?');
    return question_mark == string::npos
        ? path
        : path.substr(0, question_mark);
}

map<string, string> parseQuery(const string& path)
{
    map<string, string> result;
    const size_t question_mark = path.find('?');
    if(question_mark == string::npos)
    {
        return result;
    }

    string query = path.substr(question_mark + 1);
    size_t begin = 0;
    while(begin <= query.size())
    {
        const size_t ampersand = query.find('&', begin);
        const string item = query.substr(
            begin,
            ampersand == string::npos
                ? string::npos
                : ampersand - begin);
        const size_t equals = item.find('=');
        if(equals != string::npos)
        {
            result[item.substr(0, equals)] =
                item.substr(equals + 1);
        }

        if(ampersand == string::npos)
        {
            break;
        }
        begin = ampersand + 1;
    }

    return result;
}

bool parseUnsigned(
    const string& text,
    std::uint64_t& value)
{
    try
    {
        size_t consumed = 0;
        const unsigned long long parsed =
            stoull(text, &consumed, 0);
        if(consumed != text.size())
        {
            return false;
        }
        value = static_cast<std::uint64_t>(parsed);
        return true;
    }
    catch(...)
    {
        return false;
    }
}

string frameToJson(const CanFrame& frame)
{
    ostringstream output;
    output << "{\"sequence\":" << frame.sequence
           << ",\"timestamp_ns\":" << frame.timestamp_ns
           << ",\"id\":" << frame.id
           << ",\"id_hex\":\"0x"
           << hex << uppercase << frame.id << nouppercase << dec
           << "\",\"extended\":"
           << (frame.is_extended ? "true" : "false")
           << ",\"dlc\":"
           << static_cast<unsigned>(frame.dlc)
           << ",\"data\":[";

    for(size_t index = 0; index < frame.dlc; ++index)
    {
        if(index != 0)
        {
            output << ',';
        }
        output << static_cast<unsigned>(frame.data[index]);
    }

    output << "]}";
    return output.str();
}

string framesToJson(const vector<CanFrame>& frames)
{
    ostringstream output;
    output << "{\"frames\":[";
    for(size_t index = 0; index < frames.size(); ++index)
    {
        if(index != 0)
        {
            output << ',';
        }
        output << frameToJson(frames[index]);
    }
    output << "]}";
    return output.str();
}

string frameToSse(const CanFrame& frame)
{
    ostringstream output;
    output << "event: can_frame\n"
           << "id: " << frame.sequence << "\n"
           << "data: " << frameToJson(frame) << "\n\n";
    return output.str();
}

string statisticsToJson(
    const CanBusStatisticsSnapshot& statistics)
{
    ostringstream output;
    output << "{\"total_received_frames\":"
           << statistics.total_received_frames
           << ",\"total_received_payload_bytes\":"
           << statistics.total_received_payload_bytes
           << ",\"receive_rate_fps\":"
           << statistics.receive_rate_fps
           << ",\"estimated_bus_load_percent\":"
           << statistics.estimated_bus_load_percent
           << ",\"per_id\":[";

    for(size_t index = 0;
        index < statistics.per_id.size();
        ++index)
    {
        if(index != 0)
        {
            output << ',';
        }

        const auto& item = statistics.per_id[index];
        output << "{\"id\":" << item.id
               << ",\"id_hex\":\"0x"
               << hex << uppercase << item.id << nouppercase << dec
               << "\",\"extended\":"
               << (item.is_extended ? "true" : "false")
               << ",\"frame_count\":"
               << item.frame_count
               << ",\"payload_bytes\":"
               << item.payload_bytes
               << "}";
    }

    output << "]}";
    return output.str();
}

bool parseSendBody(
    const string& body,
    std::uint32_t& id,
    bool& is_extended,
    vector<std::uint8_t>& data)
{
    const auto readValue = [&body](const string& key,
                                   string& value) -> bool
    {
        const string quoted_key = "\"" + key + "\"";
        const size_t key_position = body.find(quoted_key);
        if(key_position == string::npos)
        {
            return false;
        }

        const size_t colon = body.find(':', key_position);
        if(colon == string::npos)
        {
            return false;
        }

        size_t begin = colon + 1;
        while(begin < body.size() &&
              std::isspace(static_cast<unsigned char>(body[begin])))
        {
            ++begin;
        }

        size_t end = begin;
        while(end < body.size() &&
              body[end] != ',' && body[end] != '}')
        {
            ++end;
        }

        value = trim(body.substr(begin, end - begin));
        if(value.size() >= 2 &&
           value.front() == '"' &&
           value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }
        return !value.empty();
    };

    string id_text;
    if(!readValue("id", id_text))
    {
        return false;
    }

    std::uint64_t parsed_id = 0;
    if(!parseUnsigned(id_text, parsed_id) ||
       parsed_id > 0x1FFFFFFFU)
    {
        return false;
    }
    id = static_cast<std::uint32_t>(parsed_id);

    string extended_text;
    if(readValue("extended", extended_text))
    {
        if(extended_text == "true")
        {
            is_extended = true;
        }
        else if(extended_text == "false")
        {
            is_extended = false;
        }
        else
        {
            return false;
        }
    }

    const size_t data_key = body.find("\"data\"");
    if(data_key == string::npos)
    {
        return false;
    }

    const size_t begin_bracket = body.find('[', data_key);
    const size_t end_bracket = body.find(']', begin_bracket);
    if(begin_bracket == string::npos ||
       end_bracket == string::npos)
    {
        return false;
    }

    string data_text = body.substr(
        begin_bracket + 1,
        end_bracket - begin_bracket - 1);

    size_t begin = 0;
    while(begin < data_text.size())
    {
        while(begin < data_text.size() &&
              (std::isspace(static_cast<unsigned char>(data_text[begin])) ||
               data_text[begin] == ','))
        {
            ++begin;
        }
        if(begin == data_text.size())
        {
            break;
        }

        size_t end = begin;
        while(end < data_text.size() &&
              data_text[end] != ',')
        {
            ++end;
        }

        std::uint64_t byte = 0;
        if(!parseUnsigned(trim(data_text.substr(begin, end - begin)), byte) ||
           byte > 0xFFU || data.size() >= CanFrame::kMaxDataLength)
        {
            return false;
        }

        data.push_back(static_cast<std::uint8_t>(byte));
        begin = end;
    }

    return true;
}
}

Connection::Connection(
    int fd,
    EventLoop* loop,
    CanRuntime* can_runtime)
    : fd_(fd),
      loop(loop),
      is_closed(false),
      can_runtime_(can_runtime)
{
}

Connection::~Connection()
{
    is_closed = true;
    LOG_INFO("connection destroyed, fd=" + to_string(fd_));
}

void Connection::appendSendBuf(const string& data)
{
    if(data.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    send_buf += data;
}

void Connection::startSse(std::uint64_t after_sequence)
{
    std::lock_guard<std::mutex> state_lock(sse_mutex_);
    sse_after_sequence_ = after_sequence;
    sse_last_heartbeat_ = std::chrono::steady_clock::now();

    appendSendBuf(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream; charset=utf-8\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n"
        ": connected\n\n");
    sse_active_.store(true, std::memory_order_release);
}

bool Connection::appendSsePayload(const std::string& payload)
{
    if(payload.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    if(send_buf.size() + payload.size() > kMaxSsePendingBytes)
    {
        sse_overflow_.store(true, std::memory_order_release);
        return false;
    }

    send_buf += payload;
    return true;
}

bool Connection::pumpSse()
{
    if(!isSse() || can_runtime_ == nullptr || sseOverflowed())
    {
        return false;
    }

    std::lock_guard<std::mutex> state_lock(sse_mutex_);
    if(!isSse() || sseOverflowed())
    {
        return false;
    }

    bool appended = false;
    const auto frames = can_runtime_->queryFrames(
        sse_after_sequence_,
        std::nullopt,
        100);

    for(const CanFrame& frame : frames)
    {
        sse_after_sequence_ =
            std::max(sse_after_sequence_, frame.sequence);
        if(!appendSsePayload(frameToSse(frame)))
        {
            return appended;
        }
        appended = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if(now - sse_last_heartbeat_ >= std::chrono::seconds(10))
    {
        appended = appendSsePayload(": heartbeat\n\n") || appended;
        sse_last_heartbeat_ = now;
    }

    return appended;
}

void Connection::handleRead()
{
    std::lock_guard<std::mutex> read_lock(read_mutex_);
    char buf[4096];

    while(true)
    {
        ssize_t n = read(fd_, buf, sizeof(buf));
        if(n > 0)
        {
            recv_buf.append(buf, static_cast<size_t>(n));
        }
        else if(n == 0)
        {
            LOG_INFO("fd=" + to_string(fd_) + " client closed connection");
            loop->removeEvent(fd_, this);
            return;
        }
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            LOG_ERROR("fd=" + to_string(fd_) + " read failed: " + string(strerror(errno)));
            loop->removeEvent(fd_, this);
            return;
        }
    }

    while(true)
    {
        size_t header_end = recv_buf.find("\r\n\r\n");
        if(header_end == string::npos)
        {
            break;
        }

        size_t content_length = extractContentLength(recv_buf);
        size_t request_len = header_end + 4 + content_length;
        if(recv_buf.size() < request_len)
        {
            break;
        }

        string request_text = recv_buf.substr(0, request_len);
        if(!req.parse(request_text))
        {
            string response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Content-Length: 11\r\n"
                "\r\n"
                "Bad Request";
            appendSendBuf(response);
            loop->updateEvent(fd_, EPOLLOUT, this);
            recv_buf.clear();
            return;
        }

        processRequest();
        recv_buf.erase(0, request_len);
        req = HttpRequest();
    }
}

void Connection::processRequest()
{
    string method_str;
    if(req.getMethod() == Method::GET)
    {
        method_str = "GET";
    }
    else if(req.getMethod() == Method::POST)
    {
        method_str = "POST";
    }
    else
    {
        method_str = "UNKNOWN";
    }

    LOG_INFO("method=" + method_str + " path=" + req.getPath());

    const string route = pathWithoutQuery(req.getPath());

    if(req.getMethod() == Method::GET &&
       route == "/api/can/frames")
    {
        HttpResponse resp;
        resp.setHeader("Content-Type", "application/json; charset=utf-8");

        if(can_runtime_ == nullptr)
        {
            resp.setStatusCode(503);
            resp.setBody("{\"error\":\"CAN runtime unavailable\"}");
        }
        else
        {
            const auto query = parseQuery(req.getPath());
            std::uint64_t after = 0;
            std::uint64_t limit_value = 50;
            std::optional<std::uint32_t> id_filter;
            bool request_is_valid = true;

            if(query.count("after") != 0 &&
               !parseUnsigned(query.at("after"), after))
            {
                resp.setStatusCode(400);
                resp.setBody("{\"error\":\"invalid after\"}");
                request_is_valid = false;
            }
            else if(query.count("limit") != 0 &&
                    (!parseUnsigned(query.at("limit"), limit_value) ||
                     limit_value == 0 || limit_value > 1000))
            {
                resp.setStatusCode(400);
                resp.setBody("{\"error\":\"invalid limit\"}");
                request_is_valid = false;
            }

            if(request_is_valid && query.count("id") != 0)
            {
                std::uint64_t parsed_id = 0;
                if(!parseUnsigned(query.at("id"), parsed_id) ||
                   parsed_id > 0x1FFFFFFFU)
                {
                    resp.setStatusCode(400);
                    resp.setBody("{\"error\":\"invalid id\"}");
                    request_is_valid = false;
                }
                else
                {
                    id_filter = static_cast<std::uint32_t>(parsed_id);
                }
            }

            if(request_is_valid)
            {
                resp.setBody(framesToJson(
                    can_runtime_->queryFrames(
                        after,
                        id_filter,
                        static_cast<size_t>(limit_value))));
            }
        }

        appendSendBuf(resp.serialize());
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
        return;
    }

    if(req.getMethod() == Method::GET &&
       route == "/api/can/stats")
    {
        HttpResponse resp;
        resp.setHeader("Content-Type", "application/json; charset=utf-8");

        if(can_runtime_ == nullptr)
        {
            resp.setStatusCode(503);
            resp.setBody("{\"error\":\"CAN runtime unavailable\"}");
        }
        else
        {
            resp.setBody(statisticsToJson(
                can_runtime_->busStats()));
        }

        appendSendBuf(resp.serialize());
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
        return;
    }

    if(req.getMethod() == Method::GET &&
       route == "/api/can/stream")
    {
        if(can_runtime_ == nullptr)
        {
            HttpResponse resp;
            resp.setStatusCode(503);
            resp.setHeader(
                "Content-Type",
                "application/json; charset=utf-8");
            resp.setBody(
                "{\"error\":\"CAN runtime unavailable\"}");
            appendSendBuf(resp.serialize());
            loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
            return;
        }

        const auto query = parseQuery(req.getPath());
        std::uint64_t after = 0;
        if(query.count("after") != 0 &&
           !parseUnsigned(query.at("after"), after))
        {
            HttpResponse resp;
            resp.setStatusCode(400);
            resp.setHeader(
                "Content-Type",
                "application/json; charset=utf-8");
            resp.setBody("{\"error\":\"invalid after\"}");
            appendSendBuf(resp.serialize());
            loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
            return;
        }

        startSse(after);
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
        return;
    }

    if(req.getMethod() == Method::POST &&
       route == "/api/can/send")
    {
        HttpResponse resp;
        resp.setHeader("Content-Type", "application/json; charset=utf-8");

        std::uint32_t id = 0;
        bool is_extended = false;
        vector<std::uint8_t> data;
        string error;

        if(can_runtime_ == nullptr)
        {
            resp.setStatusCode(503);
            resp.setBody("{\"error\":\"CAN runtime unavailable\"}");
        }
        else if(!parseSendBody(
                    req.getBody(), id, is_extended, data))
        {
            resp.setStatusCode(400);
            resp.setBody(
                "{\"error\":\"body must contain id and data[]\"}");
        }
        else if(!can_runtime_->sendFrame(
                    id, is_extended, data, &error))
        {
            resp.setStatusCode(500);
            resp.setBody("{\"error\":\"CAN send failed\"}");
        }
        else
        {
            resp.setBody("{\"sent\":true}");
        }

        appendSendBuf(resp.serialize());
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
        return;
    }

    if(req.getMethod() == Method::GET)
    {
        string urlpath = route;
        if(urlpath == "/")
        {
            urlpath = "/index.html";
        }

        const string wwwroot = "wwwroot";
        string filepath = wwwroot + urlpath;

        HttpResponse resp;
        ifstream file(filepath, ios::binary | ios::ate);
        if(!file.is_open())
        {
            resp.setStatusCode(404);
            resp.setHeader("Content-Type", "text/html; charset=utf-8");
            resp.setBody("<h1>404 Not Found</h1><p>The requested resource was not found.</p>");
        }
        else
        {
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", HttpResponse::getMimeType(filepath));
            streamsize size = file.tellg();
            file.seekg(0, ios::beg);
            string content(static_cast<size_t>(size), '\0');
            file.read(&content[0], size);
            resp.setBody(content);
        }

        appendSendBuf(resp.serialize());
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
    }
    else if(req.getMethod() == Method::POST)
    {
        HttpResponse resp;
        resp.setStatusCode(200);
        resp.setHeader("Content-Type", "text/html; charset=utf-8");

        string html = "<h1>POST request received</h1>";
        html += "<p>Request body:</p>";
        html += "<pre>" + req.getBody() + "</pre>";
        resp.setBody(html);

        appendSendBuf(resp.serialize());
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
    }
    else
    {
        HttpResponse resp;
        resp.setStatusCode(405);
        resp.setHeader("Content-Type", "text/html; charset=utf-8");
        resp.setBody("<h1>405 Method Not Allowed</h1>");
        appendSendBuf(resp.serialize());
        loop->updateEvent(fd_, EPOLLIN | EPOLLOUT, this);
    }
}

void Connection::handleWrite()
{
    std::lock_guard<std::mutex> write_lock(write_mutex_);

    while(true)
    {
        string pending;
        {
            std::lock_guard<std::mutex> lock(send_mutex_);
            if(send_buf.empty())
            {
                break;
            }
            pending = send_buf;
        }

        ssize_t n = write(fd_, pending.data(), pending.size());
        if(n > 0)
        {
            std::lock_guard<std::mutex> lock(send_mutex_);
            send_buf.erase(0, static_cast<size_t>(n));
        }
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }

            LOG_ERROR("fd=" + to_string(fd_) + " send failed: " + string(strerror(errno)));
            loop->removeEvent(fd_, this);
            return;
        }
    }

    loop->updateEvent(fd_, EPOLLIN, this);
}
