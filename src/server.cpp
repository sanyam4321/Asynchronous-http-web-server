#include "connection_types.h"
#include "data_types.h"
#include "listening_server.h"
#include "reactor.h"
#include "socket_utilities.h"

void printHttpRequest(const FiberConn::HttpRequest* request)
{
    std::cout << "HTTP Request:" << std::endl;
    std::cout << "  Method:  " << request->method << std::endl;
    std::cout << "  URL:     " << request->URL << std::endl;
    std::cout << "  Version: " << request->version << std::endl;
    std::cout << "  Headers:" << std::endl;
    for (const auto& header : request->headers) {
        std::cout << "    " << header.first << ": " << header.second << std::endl;
    }
    std::cout << "  Body:" << std::endl;
    if (!request->body.empty()) {
        std::string bodyString(request->body.begin(), request->body.end());
        std::cout << bodyString << std::endl;
    } else {
        std::cout << "    [empty]" << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Too few arguments\n";
        return 1;
    }
    FiberConn::IOReactor *ioc = new FiberConn::IOReactor(10000);
    FiberConn::HttpServer *server = new FiberConn::HttpServer(ioc, argv[1], argv[2]);
    server->listen([](void *new_con){
        auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
        client->read([](void *new_con){
            auto *client = static_cast<FiberConn::Clientconnection *>(new_con);

            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 13\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Hello, World!";
            client->sendBuffer.insert(client->sendBuffer.end(), response.begin(), response.end());
            client->write([](void *new_con){
                auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
                delete client;
            });
        });
    });

    return 0;
}