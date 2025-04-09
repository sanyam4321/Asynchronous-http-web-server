#pragma once
#include "connection_types.h"
#include "reactor.h"
#include "socket_utilities.h"
#include <functional>

#define CONN_BACKLOG 100000

namespace FiberConn {
class HttpServer {
public:
    int socket;
    int status;
    FiberConn::IOReactor* ioc;
    HttpServer(IOReactor* ioc, char* address, char* port)
    {
        this->ioc = ioc;
        this->status = 0;
        struct addrinfo* endpoint = FiberConn::getEndpoint(AF_INET, true, address, port);
        socket = FiberConn::getSocket(endpoint, true, false);
        FiberConn::bindAndListen(socket, endpoint, CONN_BACKLOG);
    }
    int listen(std::function<void(void *)> cb)
    {
        status = this->ioc->asyncAccept(socket, [this, cb](struct epoll_event ev1) {
            /*Check for any disconnections*/
            if (ev1.events & EPOLLERR) {
                /*clean up*/
            } else if (ev1.events & EPOLLIN) {
                /*accept and register a new socket*/
                int newfd = FiberConn::acceptConnection(ev1.data.fd);
                if (newfd == -1) {
                    /*clean up*/
                } else {
                    /*create a new connection object*/
                    FiberConn::Clientconnection* client = new FiberConn::Clientconnection(newfd, this->ioc);
                    cb(client);
                }
            }
        });
        if (status == -1) {
            /*clean up*/
        } else {
            ioc->reactorRun();
        }
        return 0;
    }
};
}