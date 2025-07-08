#include "connection_types.h"
#include "data_types.h"
#include "listening_server.h"
#include "reactor.h"
#include "socket_utilities.h"
#include <sstream>
#include "db_pooler.h"
#include <csignal>
#include "json.hpp"


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
    
    std::string conninfo = "host=127.0.0.1 port=5432 dbname=mydatabase user=postgres password=mypassword";
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
            if(client->request->URL == "/"){
                
                nlohmann::json j = nlohmann::json::parse(client->request->body, nullptr, false);
                
                std::string body;

                if(j.is_discarded() == false){
                    int id = j.value("id", 0);
                    std::string title = j.value("title", "");
                    body = j.dump();
                }

                
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
            
            nlohmann::json j = nlohmann::json::parse(client->request->body, nullptr, false);

            if(j.is_discarded()){
                delete client;
                return;
            }
            
            int id = j.value("id", 0);
            std::string title = j.value("title", "");

            std::string query = "INSERT INTO books (id, title) VALUES (" + std::to_string(id) + ", '" + title + "');";
            pooler->sendQuery(query, client->connectionId, [](void *conn){
                auto *dbconnection = static_cast<FiberConn::Dbconnection *>(conn);
                if(dbconnection->is_error){
                    std::cerr<<"db query error\n";
                    return;
                }
                FiberConn::Clientconnection *client = dbconnection->getParent();
                if(client == nullptr) {
                    return;
                }

                nlohmann::json j;
            
                for (const auto& result : dbconnection->results) {
                    int nrows = result.rows, ncols = result.cols;
                    for (int i = 0; i < nrows; ++i) {
                        j.push_back({{"id", result.table[i][0]}, {"title", result.table[i][1]}});
                    }
                }
                std::string body = j.dump();

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