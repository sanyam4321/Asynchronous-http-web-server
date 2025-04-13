#pragma once
#include <functional>
#include <unordered_map>
#include <utility>
#include "data_types.h"
#include "llhttp.h"
#include "reactor.h"
#include <cstring>
#include "postgresql/libpq-fe.h"

namespace FiberConn
{

    class Clientconnection;
    extern std::unordered_map<Clientconnection *, bool> isAlive;

        class Clientconnection
        {
        public:
            int socket;
            IOReactor *ioc;
        

            Clientconnection *parent = nullptr;
            bool is_error = false;
            bool is_request_complete = false;
            HttpRequest *request;

            llhttp_t *parser;
            llhttp_settings_t *settings;

            char recvBuffer[1024];

            size_t sent_bytes;
            std::vector<char> sendBuffer;

            Clientconnection(int sockfd, IOReactor *ioc)
            {
                this->socket = sockfd;
                this->ioc = ioc;

                request = new HttpRequest();
                memset(recvBuffer, 0, sizeof(recvBuffer));
                sent_bytes = 0;

                parser = new llhttp_t();
                settings = new llhttp_settings_t();

                llhttp_settings_init(settings);

                settings->on_message_begin = on_message_begin;
                settings->on_method = on_method;
                settings->on_url = on_url;
                settings->on_version = on_version;
                settings->on_header_field = on_header_field;
                settings->on_header_value = on_header_value;
                settings->on_headers_complete = on_headers_complete;
                settings->on_header_value_complete = on_header_value_complete;
                settings->on_body = on_body;
                settings->on_message_complete = on_message_complete;

                llhttp_init(parser, HTTP_REQUEST, settings);
                parser->data = static_cast<void *>(this);
                isAlive[this] = true;
            }
            ~Clientconnection()
            {
                ioc->removeTrack(this->socket);
                closeConnection(this->socket);
                delete request;
                delete parser;
                delete settings;

                auto it = isAlive.find(this);
                if(it != isAlive.end()){
                    isAlive.erase(it);
                }
            }

            static int on_message_begin(llhttp_t *parser)
            {
                // std::cout << "[Callback] Message begin\n";
                return 0;
            }

            static int on_method(llhttp_t *parser, const char *at, size_t length)
            {
                // std::cout<<"on method\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->method.append(at, length);
                return 0;
            }

            static int on_url(llhttp_t *parser, const char *at, size_t length)
            {
                // std::cout<<"on url\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->URL.append(at, length);
                return 0;
            }

            static int on_version(llhttp_t *parser, const char *at, size_t length)
            {
                // std::cout<<"on version\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->version.append(at, length);
                return 0;
            }

            static int on_header_field(llhttp_t *parser, const char *at, size_t length)
            {
                // std::cout<<"on header field\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->key.append(at, length);
                return 0;
            }

            static int on_header_value(llhttp_t *parser, const char *at, size_t length)
            {
                // std::cout<<"on header value\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->value.append(at, length);
                return 0;
            }

            static int on_header_value_complete(llhttp_t *parser)
            {
                // std::cout<<"on header value complete\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->headers[request->key] = request->value;
                request->key.clear();
                request->value.clear();
                return 0;
            }

            static int on_headers_complete(llhttp_t *parser) { /*std::cout<<"on header complete\n";*/ return 0; }

            static int on_body(llhttp_t *parser, const char *at, size_t length)
            {
                // std::cout<<"on body\n";
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                HttpRequest *request = conn->request;
                request->body.insert(request->body.end(), at, at + length);
                return 0;
            }

            static int on_message_complete(llhttp_t *parser)
            {
                // std::cout<<"message complete\n";
                llhttp_pause(parser);
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                conn->is_request_complete = true;
                return 0;
            }

            
            void write(std::function<void(void *)> cb)
            {
                uint32_t mask = EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
                ioc->modifyTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                    this->handleEvent(event, cb); 
                });
            }

            void read(std::function<void(void *)> cb)
            {
                uint32_t mask = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
                this->ioc->addTrack(this->socket, mask, NEW_SOCK, [this, cb](struct epoll_event event) {
                    this->handleEvent(event, cb);
                });       
            }

        void close() 
        { 
            closeConnection(socket); 
        }
        void handleEvent(struct epoll_event ev, std::function<void(void *)> cb)
        {
            if (ev.events & EPOLLERR)
            {
                /*user will close the connection and delete its memory*/
                this->is_error = true;
                cb(this);
                return;
            }
            else if (ev.events & EPOLLIN)
            {
                int read_bytes;
                memset(recvBuffer, 0, sizeof(recvBuffer));
                while ((read_bytes = recv(this->socket, this->recvBuffer, sizeof(this->recvBuffer), MSG_DONTWAIT)) > 0)
                {
                    llhttp_errno_t llerror = llhttp_execute(this->parser, recvBuffer, read_bytes);
                    if (llerror != HPE_OK)
                    {
                        std::cerr << "parsing error: " << llhttp_errno_name(llerror) << " parser reason: " << parser->reason << "\n";
                        this->is_error = true;
                        cb(this);
                        return;
                    }
                    if(this->is_request_complete == true){
                        cb(this);
                        return;
                    }
                    memset(recvBuffer, 0, sizeof(recvBuffer));
                }
                /* what happens if error */
                if (read_bytes == 0)
                {
                    this->is_error = true;
                    cb(this);
                    return;
                }
            }
            else if (ev.events & EPOLLOUT)
            {
                int bytes_sent;
                while ((bytes_sent = send(this->socket, this->sendBuffer.data() + this->sent_bytes, this->sendBuffer.size() - this->sent_bytes, MSG_DONTWAIT)) > 0)
                {
                    this->sent_bytes += bytes_sent;

                    if (this->sent_bytes >= this->sendBuffer.size())
                    {
                        // All bytes sent
                        this->sendBuffer.clear();
                        this->sent_bytes = 0;
                        cb(this);
                        return;
                    }
                }

                if (bytes_sent == -1)
                {
                    std::cerr<<"Bytes Sent error\n";
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        return;
                    }
                    else
                    {
                        is_error = true;
                        cb(this);
                        return;
                    }
                }
            }
        }
    };

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
        int socket;
        IOReactor *ioc;
    
        DbConnectionState connection_state;

        Clientconnection *parent = nullptr;
        int parent_socket;

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

            uint32_t mask = EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
            this->ioc->addTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event) {
                this->handleEvent(event, cb); 
            });    
        }

        void handleEvent(struct epoll_event ev, std::function<void(void *)> cb){

            if(ev.events & EPOLLERR){
                is_error = true;
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
                int status = PQflush(conn);
                if(status == -1){
                    is_error = true;
                    this->ioc->removeTrack(this->socket);
                    this->connection_state = DbConnectionState::IDLE;
                    cb(this);
                    return;
                }
                else if(status == 0){
                    this->connection_state = DbConnectionState::READING_RESPONSE;
                    uint32_t mask = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
                    ioc->modifyTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                        this->handleEvent(event, cb); 
                    });
                    return;
                }
                else if(status == 1){
                    /*pending flushing*/
                    return;
                }
            }
            else if(this->connection_state == DbConnectionState::READING_RESPONSE){
                int status = PQconsumeInput(conn);
                if(status == 0){
                    is_error = true;
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
                        if (PQresultStatus(res) != PGRES_TUPLES_OK){
                            std::cerr<<PQerrorMessage(conn)<<"\n";
                            PQclear(res);
                            is_error = true;
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
                parent_ptr = this->parent;
            }
            return parent_ptr;
        }

        void resetConnection(){
            this->parent = nullptr;
            this->parent_socket = 0;
            this->results.clear();
        }

        Dbconnection(Clientconnection *parent, IOReactor *ioc)
        {
            this->ioc = ioc;
            this->parent = parent;
            if(this->parent != nullptr){
                this->parent_socket = parent->socket;
            }
            this->connection_state = DbConnectionState::NOT_CONNECTED;
            conn = NULL;
        }

        ~Dbconnection(){
            ioc->removeTrack(this->socket);
            PQfinish(this->conn);
        }

    };
} // namespace FiberConn