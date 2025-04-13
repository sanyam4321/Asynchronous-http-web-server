#pragma once
#include "connection_types.h"

namespace FiberConn
{

    struct PendingQuery{
        std::string query;
        Clientconnection *client;
        std::function<void(void *)> cb;
    };

    class DBpooler{
    public:
        int pool_size; 
        int connected_count;
        int idle_count;
        std::string connection_info;
        std::vector<Dbconnection *> connected_conns; 
        std::queue<Dbconnection *> available_conns;

        bool is_ready = false;
        IOReactor *ioc;


        std::queue<PendingQuery> pending_queries;

        DBpooler(IOReactor *ioc, std::string conninfo, int pool_size){
            this->ioc = ioc;
            this->pool_size = pool_size;
            this->connected_count = 0;
            this->idle_count = 0;
            this->connection_info = conninfo;

            char conn_info[conninfo.length() + 1];
            std::strcpy(conn_info, conninfo.c_str());

            for(int i=0; i<pool_size; i++){
                Dbconnection *dbconn = new Dbconnection(nullptr, ioc);
                dbconn->connectDb(conn_info, [this](void *new_dbconn){
                    auto *dbconn = static_cast<Dbconnection *>(new_dbconn);
                    if(dbconn->is_error == false){
                        this->connected_conns.push_back(dbconn);
                        this->available_conns.push(dbconn);
                        this->connected_count++;
                        this->idle_count++;
                        std::cout<<"db connection success: "<< this->idle_count << "\n";
                        if(this->connected_count == this->pool_size){
                            this->is_ready = true;
                        }
                    }
                    else {
                        std::cerr<<"db connection failed: "<<this->idle_count<< "\n";
                    }
                });
            }
        }
        ~DBpooler(){
            for(int i=0; i<pool_size; i++){
                auto *conn = connected_conns[i];
                delete conn;
            }
        }
        
        void sendQuery(std::string query, Clientconnection *client, std::function<void (void *)> cb){
            if(idle_count > 0){
                auto *dbconn = this->available_conns.front();
                this->available_conns.pop();
                this->idle_count--;

                dbconn->parent = client;
                dbconn->parent_socket = client->socket;

                char query_string[query.length()+1];
                std::strcpy(query_string, query.c_str());
                
                dbconn->sendQuery(query_string, [this, cb](void *conn){
                    
                    auto *dbconn = static_cast<Dbconnection *> (conn);
                    cb(dbconn);
                    dbconn->resetConnection();
                    this->idle_count++;
                    this->available_conns.push(dbconn);

                    while(this->idle_count > 0 && this->pending_queries.size() > 0){
                        PendingQuery temp = this->pending_queries.front();
                        this->pending_queries.pop();
                        this->sendQuery(temp.query, temp.client, temp.cb);
                    }

                });
            }
            else {
                PendingQuery temp;
                temp.query = query;
                temp.client = client;
                temp.cb = cb;
                this->pending_queries.push(temp);
            }
        }


    };

}