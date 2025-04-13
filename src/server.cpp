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

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    if (argc < 3) {
        std::cerr << "Too few arguments\n";
        return 1;
    }
    
    FiberConn::IOReactor *ioc = new FiberConn::IOReactor(10000);

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