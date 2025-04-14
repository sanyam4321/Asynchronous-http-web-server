#include "connection_types.h"
#include "data_types.h"
#include "listening_server.h"
#include "reactor.h"
#include "socket_utilities.h"
#include <sstream>
#include "db_pooler.h"
#include <csignal>

namespace FiberConn {
    std::unordered_map<Clientconnection*, bool> isAlive;
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

    
    // FiberConn::APIconnection *apiconn = new FiberConn::APIconnection(nullptr, ioc);

    // std::string address(argv[1]);
    // std::string port(argv[2]);
    // apiconn->connectApi(address, port, [](void *conn){
    //     FiberConn::APIconnection *apiconn = static_cast<FiberConn::APIconnection *>(conn);
    //     std::cout<<apiconn->socket<<"\n";
    //     if(apiconn->is_error == false){
    //         std::cout<<"successfully connected\n";
            
    //         std::ostringstream oss;
    //         oss << "GET / HTTP/1.1\r\n";
    //         oss << "Host: www.habfix-gray.vercel.app\r\n";
    //         oss << "User-Agent: FiberConn/1.0\r\n";
    //         oss << "Accept: */*\r\n";
    //         oss << "Connection: close\r\n";
    //         oss << "\r\n";

    //         std::string request = oss.str();

    //         apiconn->sendBuffer.insert(apiconn->sendBuffer.end(), request.begin(), request.end());

    //         apiconn->sendRequest([](void *conn){
    //             FiberConn::APIconnection *apiconn = static_cast<FiberConn::APIconnection *>(conn);
    //             if(apiconn->is_error){
    //                 std::cout<<"error\n";
    //                 delete apiconn;
    //                 return;
    //             }
    //             printHttpResponse(apiconn->response);
    //             delete apiconn;
    //         });
    //     }
    //     else{
    //         std::cout<<"connection failed\n";
    //         delete apiconn;
    //     }
    // });

    
   
    
    std::string conninfo = "host=127.0.0.1 port=5432 dbname=mydatabase user=myuser password=mypassword";
    FiberConn::DBpooler *pooler = new FiberConn::DBpooler(ioc, conninfo, 100);
    FiberConn::HttpServer *server = new FiberConn::HttpServer(ioc, argv[1], argv[2]);

    server->listen([ioc, pooler](void *new_con){
        auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
        
        client->read([ioc, pooler](void *new_con){
            auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
            if(client->is_error){
                delete client;
                return;
            }
            if(client->request->URL == "/books"){
                std::string body = "Hello World";

                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: text/plain\r\n"
                    << "Content-Length: " << body.size() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << body;
                std::string response = oss.str();    
                client->sendBuffer.insert(client->sendBuffer.end(), response.begin(), response.end());

                if(client != nullptr) {
                    client->write([](void *new_con){
                        auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
                        if(client != nullptr) delete client;
                    });
                }
                return;
            }
            


            std::string query = "SELECT * from books;";
            pooler->sendQuery(query, client, [](void *conn){
                auto *dbconnection = static_cast<FiberConn::Dbconnection *>(conn);
                if(dbconnection->is_error){
                    std::cerr<<"db query error\n";
                    return;
                }
                FiberConn::Clientconnection *client = dbconnection->getParent();
                if(client == nullptr) {
                    return;
                }
                std::ostringstream bodyStream;

                for (const auto& result : dbconnection->results) {
                    int nrows = result.rows, ncols = result.cols;
                    for (int i = 0; i < nrows; ++i) {
                        for (int j = 0; j < ncols; ++j) {
                            bodyStream << result.table[i][j];
                            if (j < ncols - 1) bodyStream << ", ";
                        }
                        bodyStream << "\n";
                    }
                }
                std::string body = bodyStream.str();

                std::ostringstream oss;
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: text/plain\r\n"
                    << "Content-Length: " << body.size() << "\r\n"
                    << "Connection: close\r\n"
                    << "\r\n"
                    << body;
                std::string response = oss.str();    
                client->sendBuffer.insert(client->sendBuffer.end(), response.begin(), response.end());

                if(client != nullptr) {
                    client->write([](void *new_con){
                        auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
                        if(client != nullptr) delete client;
                    });
                }
            });
        });
    });
    
    ioc->reactorRun();
    return 0;
}