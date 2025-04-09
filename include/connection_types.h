#pragma once
#include <functional>
#include <unordered_map>
#include <utility>
#include "data_types.h"
#include "llhttp.h"
#include "reactor.h"

namespace FiberConn
{
    class Clientconnection
    {
    private:
        int socket;
        IOReactor *ioc;
    public:
        void *parent = nullptr;

        bool is_error = false;
        HttpRequest *request;

        llhttp_t *parser;
        llhttp_settings_t *settings;

        size_t recv_buffer_size;
        std::vector<char> recvBuffer;

        size_t sent_bytes;
        std::vector<char> sendBuffer;

        Clientconnection(int sockfd, IOReactor *ioc)
        {
            this->socket = sockfd;
            this->ioc = ioc;

            request = new HttpRequest();

            recv_buffer_size = 1024;
            recvBuffer = std::vector<char>(recv_buffer_size, 0);

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
            auto *dataPair = new std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>(this, nullptr);
            parser->data = static_cast<void *>(dataPair);
        }
        ~Clientconnection()
        {
            closeConnection(this->socket);
            delete request;
            delete parser;
            delete settings;
            delete static_cast<std::pair<Clientconnection*, std::function<void(void*)>>*>(parser->data);
        }

        static int on_message_begin(llhttp_t *parser)
        {
            // std::cout << "[Callback] Message begin\n";
            return 0;
        }

        static int on_method(llhttp_t *parser, const char *at, size_t length)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->method.append(at, length);
            return 0;
        }

        static int on_url(llhttp_t *parser, const char *at, size_t length)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->URL.append(at, length);
            return 0;
        }

        static int on_version(llhttp_t *parser, const char *at, size_t length)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->version.append(at, length);
            return 0;
        }

        static int on_header_field(llhttp_t *parser, const char *at, size_t length)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->key.append(at, length);
            return 0;
        }

        static int on_header_value(llhttp_t *parser, const char *at, size_t length)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->value.append(at, length);
            return 0;
        }

        static int on_header_value_complete(llhttp_t *parser)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->headers[request->key] = request->value;
            request->key.clear();
            request->value.clear();
            return 0;
        }

        static int on_headers_complete(llhttp_t *parser) { return 0; }

        static int on_body(llhttp_t *parser, const char *at, size_t length)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            HttpRequest *request = conn->request;
            request->body.insert(request->body.end(), at, at + length);
            return 0;
        }

        static int on_message_complete(llhttp_t *parser)
        {
            auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(parser->data);
            FiberConn::Clientconnection* conn = dataPair->first;
            dataPair->second(conn);
            return 0;
        }

        
        void write(std::function<void(void *)> cb)
        {
            uint32_t mask = EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
            ioc->modifyTrack(this->socket, mask, HELPER_SOCK, [this, cb](struct epoll_event event){ 
                this->handleEvent(event, cb); 
            });
        }

        void read(std::function<void(void *)> cb)
        {
            uint32_t mask = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
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
                while ((read_bytes = recv(this->socket, recvBuffer.data(), recvBuffer.size(), MSG_DONTWAIT)) > 0)
                {
                    auto* dataPair = static_cast<std::pair<FiberConn::Clientconnection*, std::function<void(void*)>>*>(this->parser->data);
                    dataPair->second = cb;
                    llhttp_errno_t llerror = llhttp_execute(this->parser, recvBuffer.data(), read_bytes);
                    if (llerror != HPE_OK)
                    {
                        std::cerr << "parsing error: " << llhttp_errno_name(llerror) << " parser reason: " << parser->reason << "\n";
                        this->is_error = true;
                        cb(this);
                        return;
                    }
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

} // namespace FiberConn