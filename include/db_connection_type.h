#pragma once
#include <functional>
#include <unordered_map>
#include <utility>
#include "reactor.h"
#include <cstring>
#include "postgresql/libpq-fe.h"
#include <string>
#include "http_server_type.h"

namespace FiberConn{

    extern std::unordered_map<std::string, Clientconnection *> isAlive;

    enum DbConnectionState{
        IDLE,
        NOT_CONNECTED,
        CONNECTING,
        CONNECTED,
        SENDING_QUERY,
        READING_RESPONSE
    };

    struct QueryResult{
        int rows;
        int cols;
        std::vector<std::vector<std::string>> table;
    };

    class Dbconnection{
    public:
        std::string connectionId;
        int socket;
        IOReactor *ioc;
    
        DbConnectionState connection_state;

        std::string parent;

        bool is_error = false;  

        PGconn *conn;
        std::vector<QueryResult> results; 

        void connectDb(char *conninfo, std::function<void(void *)> cb){
            this->conn = PQconnectStart(conninfo);
            if (conn == NULL){
                std::cout<<"null connection\n";
                is_error = true;
                cb(this);
                return;
            }
            this->socket = PQsocket(conn);
            this->connection_state = DbConnectionState::CONNECTING;

            /*monitor the socket for writing*/
            uint32_t mask = EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
            this->ioc->addTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event) {
                this->handleEvent(event, cb); 
            });     
        }

        void sendQuery(char *query_string, std::function<void(void *)> cb){
            if(PQsendQuery(conn, query_string) == 0){
                std::cerr<<PQerrorMessage(conn)<<"\n";
                is_error = true;
                cb(this);
                return;
            }
            this->connection_state = DbConnectionState::SENDING_QUERY;

            int status = PQflush(this->conn);
            if(status == -1){
                is_error = true;
                std::cerr<<PQerrorMessage(conn)<<"\n";
                this->connection_state = DbConnectionState::IDLE;
                cb(this);
                return;
            }
            else if(status == 0){
                this->connection_state = DbConnectionState::READING_RESPONSE;
                uint32_t mask = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
                ioc->addTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                    this->handleEvent(event, cb); 
                });
                return;
            }
            else if(status == 1){
                uint32_t mask = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
                ioc->addTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                    this->handleEvent(event, cb); 
                });
                return;
            }    
        }

        void handleEvent(struct epoll_event ev, std::function<void(void *)> cb){

            if(ev.events & EPOLLERR){
                is_error = true;
                std::cerr<<PQerrorMessage(conn)<<"\n";
                ioc->removeTrack(this->socket);
                cb(this);
                return;
            }

            if(this->connection_state == DbConnectionState::CONNECTING){
                PostgresPollingStatusType status = PQconnectPoll(this->conn);
                if (status == PGRES_POLLING_READING)
                {
                    uint32_t mask = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
                    this->ioc->modifyTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                        this->handleEvent(event, cb); 
                    });                    
                }
                else if (status == PGRES_POLLING_WRITING)
                {
                    uint32_t mask = EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
                    this->ioc->modifyTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                        this->handleEvent(event, cb); 
                    });     
                }
                else if (status == PGRES_POLLING_FAILED)
                {
                    std::cout<<"database polling failed\n";
                    is_error = true;
                    std::cerr<<PQerrorMessage(conn)<<"\n";
                    this->ioc->removeTrack(this->socket);
                    cb(this);
                    return;
                }
                else if (status == PGRES_POLLING_OK)
                {
                    this->connection_state = DbConnectionState::CONNECTED;
                    this->ioc->removeTrack(this->socket);
                    if(PQsetnonblocking(this->conn, 1) == -1){
                        std::cerr<<PQerrorMessage(conn)<<"\n";
                    }
                    cb(this);
                    return;
                }               
            }
            else if(this->connection_state == DbConnectionState::SENDING_QUERY){
                int consumeStatus = 1, flushStatus = 1;
                if(ev.events & EPOLLOUT){
                    flushStatus = PQflush(this->conn);
                    if(flushStatus == -1){
                        is_error = true;
                        std::cerr<<PQerrorMessage(conn)<<"\n";
                        this->ioc->removeTrack(this->socket);
                        cb(this);
                        return;
                    }
                }
                if(ev.events & EPOLLIN){
                    consumeStatus = PQconsumeInput(this->conn);
                    flushStatus = PQflush(this->conn);
                    if(flushStatus == -1 || consumeStatus == 0){
                        is_error = true;
                        std::cerr<<PQerrorMessage(conn)<<"\n";
                        this->ioc->removeTrack(this->socket);
                        cb(this);
                        return;
                    }
                }
                if(flushStatus == 0){
                    this->connection_state = DbConnectionState::READING_RESPONSE;
                    uint32_t mask = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
                    ioc->modifyTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                        this->handleEvent(event, cb); 
                    });
                    return;  
                }
            }
            else if(this->connection_state == DbConnectionState::READING_RESPONSE){
                int status = PQconsumeInput(conn);
                if(status == 0){
                    is_error = true;
                    std::cerr<<PQerrorMessage(conn)<<"\n";
                    this->ioc->removeTrack(this->socket);
                    this->connection_state = DbConnectionState::IDLE;
                    cb(this);
                    return;
                }
                int isBusy = PQisBusy(this->conn);
                if(isBusy == 1){
                    /*Request pending*/
                }
                else if(isBusy == 0){
                    /*Request complete*/
                    this->ioc->removeTrack(this->socket);
                    this->connection_state = DbConnectionState::IDLE;

                    /*storing all the results locally*/
                    PGresult *res;
                    while((res = PQgetResult(this->conn)) != NULL){
                        ExecStatusType queryStatus = PQresultStatus(res);
                        if (queryStatus != PGRES_TUPLES_OK && queryStatus != PGRES_COMMAND_OK){
                            std::cerr<<PQerrorMessage(conn)<<"\n";
                            
                            is_error = true;

                            while((res = PQgetResult(this->conn)) != NULL){
                                PQclear(res);
                            }
                            cb(this);
                            return;
                        }
                        int nrows = PQntuples(res);
                        int ncols = PQnfields(res);
                        
                        QueryResult temp;
                        temp.rows = nrows;
                        temp.cols = ncols;
                        

                        for(int i=0; i<nrows; i++){
                            std::vector<std::string> tuple;
                            for(int j=0; j<ncols; j++){
                                tuple.emplace_back(std::string(PQgetvalue(res, i, j)));
                            }
                            temp.table.emplace_back(tuple);
                        }

                        PQclear(res);
                        this->results.push_back(temp);
                    }
                    cb(this);
                    return;
                }
            }
        }

        Clientconnection *getParent(){
            Clientconnection *parent_ptr = nullptr;

            auto it = isAlive.find(this->parent);
            if(it != isAlive.end()){
                parent_ptr = isAlive[this->parent];
            }
            return parent_ptr;
        }

        void resetConnection(){
            this->parent = "#";
            this->results.clear();
            this->is_error = false;
        }

        Dbconnection(std::string parent, IOReactor *ioc)
        {
            this->ioc = ioc;
            if(parent == "")
                this->parent = "#";
            else
                this->parent = parent;
            this->connection_state = DbConnectionState::NOT_CONNECTED;
            conn = NULL;
        }

        ~Dbconnection(){
            ioc->removeTrack(this->socket);
            PQfinish(this->conn);
        }

    };

}