#pragma once
#include <functional>
#include <unordered_map>
#include <utility>
#include "data_types.h"
#include "llhttp.h"
#include "reactor.h"
#include <cstring>
#include "uuid_v4.h"
#include <string>


namespace FiberConn
{

    class Clientconnection;
    extern std::unordered_map<std::string, Clientconnection *> isAlive;

    class Clientconnection
    {
        public:
            std::string connectionId;
            int socket;
            IOReactor *ioc;

            bool is_error = false;
            std::string error_description;
            bool is_request_complete = false;
            HttpRequest *request;

            llhttp_t *parser;
            llhttp_settings_t *settings;

            char recvBuffer[1024];

            size_t sent_bytes;
            std::vector<char> sendBuffer;

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
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                conn->is_request_complete = true;
                return 0;
            }

            static int on_reset(llhttp_t *parser){
                FiberConn::Clientconnection* conn = static_cast<FiberConn::Clientconnection*>(parser->data);
                conn->is_error = false;
                conn->is_request_complete = false;
                delete conn->request;
                conn->request = new HttpRequest();
                memset(conn->recvBuffer, 0, sizeof(conn->recvBuffer));
                return 0;
            }

            
            void write(std::function<void(void *)> cb)
            {
                uint32_t mask = EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
                this->ioc->addTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
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

        void handleEvent(struct epoll_event ev, std::function<void(void *)> cb)
        {
            if (ev.events & EPOLLERR)
            {
                /*user will close the connection and delete its memory*/
                this->is_error = true;
                this->error_description = "connection closed abruptly";
                ioc->removeTrack(this->socket);
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
                        this->is_error = true;
                        this->error_description = "Http parsing error";
                        ioc->removeTrack(this->socket);
                        cb(this);
                        return;
                    }
                    if(this->is_request_complete == true){
                        ioc->removeTrack(this->socket);
                        cb(this);
                        return;
                    }
                    memset(recvBuffer, 0, sizeof(recvBuffer));
                }
                /* what happens if error */
                if (read_bytes == 0)
                {
                    this->is_error = true;
                    this->error_description = "client disconnected";
                    ioc->removeTrack(this->socket);
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
                        ioc->removeTrack(this->socket);
                        cb(this);
                        return;
                    }
                }

                if (bytes_sent == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        return;
                    }
                    else
                    {
                        is_error = true;
                        this->error_description = "response sending error";
                        ioc->removeTrack(this->socket);
                        cb(this);
                        return;
                    }
                }
            }
        }

        Clientconnection(int sockfd, IOReactor *ioc)
        {
            UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
            UUIDv4::UUID uuid = uuidGenerator.getUUID();
            this->connectionId = uuid.bytes();

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
            settings->on_reset = on_reset;
            
            llhttp_init(parser, HTTP_REQUEST, settings);
            parser->data = static_cast<void *>(this);
            isAlive[this->connectionId] = this;
        }
        ~Clientconnection()
        {
            closeConnection(this->socket);
            delete request;
            delete parser;
            delete settings;

            auto it = isAlive.find(this->connectionId);
            if(it != isAlive.end()){
                isAlive.erase(it);
            }
        }

    };
}