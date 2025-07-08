#include "connection_types.h"
#include "data_types.h"
#include "listening_server.h"
#include "reactor.h"
#include "socket_utilities.h"
#include <sstream>
#include "db_pooler.h"
#include <csignal>

namespace FiberConn {
    std::unordered_map<std::string, Clientconnection *> isAlive;
}

void printHttpResponse(const FiberConn::HttpResponse* response) {
    if (!response) {
        std::cerr << "Null response pointer.\n";
        return;
    }

    std::cout << "=== HTTP Response ===\n";
    std::cout << "Version: " << response->version << "\n";
    std::cout << "Status: " << response->status << "\n";
    std::cout << "Key: " << response->key << "\n";
    std::cout << "Value: " << response->value << "\n";

    std::cout << "Headers:\n";
    for (const auto& [k, v] : response->headers) {
        std::cout << "  " << k << ": " << v << "\n";
    }

    std::cout << "Body (" << response->body.size() << " bytes):\n";
    std::cout.write(response->body.data(), response->body.size());
    std::cout << "\n=====================\n";
}

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    if (argc < 3) {
        std::cerr << "Too few arguments\n";
        return 1;
    }
    
    FiberConn::IOReactor *ioc = new FiberConn::IOReactor(10000);

    
    FiberConn::APIconnection *apiconn = new FiberConn::APIconnection("", ioc);

    std::string address(argv[1]);
    std::string port(argv[2]);
    apiconn->connectApi(address, port, [](void *conn){
        FiberConn::APIconnection *apiconn = static_cast<FiberConn::APIconnection *>(conn);
        std::cout<<apiconn->socket<<"\n";
        if(apiconn->is_error == false){
            std::cout<<"successfully connected\n";
            
            std::ostringstream oss;
            oss << "GET / HTTP/1.1\r\n";
            oss << "Host: www.habfix-gray.vercel.app\r\n";
            oss << "User-Agent: FiberConn/1.0\r\n";
            oss << "Accept: */*\r\n";
            oss << "Connection: close\r\n";
            oss << "\r\n";

            std::string request = oss.str();
            std::cout<<request<<std::endl;
            apiconn->sendBuffer.insert(apiconn->sendBuffer.end(), request.begin(), request.end());

            apiconn->sendRequest([](void *conn){
                FiberConn::APIconnection *apiconn = static_cast<FiberConn::APIconnection *>(conn);
                if(apiconn->is_error){
                    std::cout<<apiconn->error_description<<std::endl;
                    printHttpResponse(apiconn->response);
                    delete apiconn;
                    return;
                }
                printHttpResponse(apiconn->response);
                delete apiconn;
            });
        }
        else{
            std::cout<<apiconn->error_description<<std::endl;
            delete apiconn;
        }
    });
    
    ioc->reactorRun();
    return 0;
}