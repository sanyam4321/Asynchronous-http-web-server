#include "connection_types.h"
#include "data_types.h"
#include "listening_server.h"
#include "reactor.h"
#include "socket_utilities.h"
#include <sstream>

namespace FiberConn {
    std::unordered_map<Clientconnection*, bool> isAlive;
}

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

    server->listen([ioc](void *new_con){
        auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
        
        std::cout<<"submitting read request\n";
        client->read([ioc](void *new_con){
            auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
            std::cout<<"client successfully read: "<<client->socket<<"\n";
            if(client->is_error){
                delete client;
                return;
            }
            std::cout<<client->request->URL<<"\n";
            if(client->request->URL == "/"){
                delete client;
                std::cout<<"client deleted\n";
                return;
            }
            std::cout<<"url not /\n";
            FiberConn::Dbconnection *db = new FiberConn::Dbconnection(client, ioc);
            char conninfo[] = "host=localhost dbname=mydatabase user=myuser password=mypassword";
            db->connectDb(conninfo, [](void *conn){
                auto *dbconnection = static_cast<FiberConn::Dbconnection *>(conn);
                
                if(dbconnection->is_error){
                    std::cerr<<"db connect error\n";
                    delete dbconnection;
                    return;
                }
                else{
                    
                    char query[] = "SELECT * from books;";
                    
                    dbconnection->sendQuery(query, [](void *conn){
                        
                        auto *dbconnection = static_cast<FiberConn::Dbconnection *>(conn);
                       
                        FiberConn::Clientconnection *client = dbconnection->getParent();
                        if(client == nullptr) {
                            delete dbconnection;
                            return;
                        }

                        if(dbconnection->is_error == true){
                            std::cout<<"db send error\n";
                            delete dbconnection;
                            delete client;    
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

                        if(dbconnection != nullptr) delete dbconnection;

                        if(client != nullptr) {
                            
                            client->write([](void *new_con){
                                auto *client = static_cast<FiberConn::Clientconnection *>(new_con);
                                std::cout<<"ending connection: "<<client->socket<<"\n";
                                if(client != nullptr) delete client;
                            });
                        }
                    });
                }
            });

            
        });
        std::cout<<"After Callback\n";
    });
    
    ioc->reactorRun();
    return 0;
}