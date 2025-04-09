#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace FiberConn {
struct HttpRequest {
    std::string method;
    std::string URL;
    std::string version;

    std::string key, value;
    std::unordered_map<std::string, std::string> headers;
    std::vector<char> body;
};

struct HttpResponse {
    std::string version;
    std::string statusCode;
    std::string statusMessage;

    std::string key, value;
    std::unordered_map<std::string, std::string> headers;
    std::vector<char> body;
};
} // namespace FiberConn