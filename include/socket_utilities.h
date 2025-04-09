#pragma once

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace FiberConn
{
    /*Create Endpoint*/
    struct addrinfo *getEndpoint(int address_family, bool will_listen, char *address, char *port)
    {
        int status;
        struct addrinfo hints;
        struct addrinfo *servlist = nullptr;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = address_family;
        hints.ai_socktype = SOCK_STREAM;
        if (will_listen)
        {
            hints.ai_flags = AI_PASSIVE;
            address = NULL;
        }

        if ((status = getaddrinfo(address, port, &hints, &servlist)) != 0)
        {
            std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
            return NULL;
        }

        struct addrinfo *server_info = nullptr;
        struct addrinfo *endpoint = nullptr;

        for (server_info = servlist; server_info != nullptr; server_info = server_info->ai_next)
        {
            char ip_string[INET6_ADDRSTRLEN];
            void *addr;
            int port;
            std::string ip_version;

            if (server_info->ai_family == AF_INET)
            {
                struct sockaddr_in *ipv4 = (struct sockaddr_in *)server_info->ai_addr;
                addr = &(ipv4->sin_addr);
                port = ntohs(ipv4->sin_port);
                ip_version = "IPV4";

                // convert addr to readable format
                inet_ntop(server_info->ai_family, addr, ip_string, sizeof(ip_string));
                // std::cout << ip_string << " " << port << " " << ip_version << std::endl;
            }
            else
            {
                struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)server_info->ai_addr;
                addr = &(ipv6->sin6_addr);
                port = ntohs(ipv6->sin6_port);
                ip_version = "IPV6";

                // convert addr to readable format
                inet_ntop(server_info->ai_family, addr, ip_string, sizeof(ip_string));
                // std::cout << ip_string << " " << port << " " << ip_version << std::endl;
            }
            if (server_info->ai_family == address_family)
            {
                endpoint = server_info;
            }
        }

        return endpoint;
    }

    /*Create Socket*/
    int getSocket(struct addrinfo *endpoint, bool will_reuse, bool will_block)
    {
        int sockfd;

        if ((sockfd = socket(endpoint->ai_family, endpoint->ai_socktype, endpoint->ai_protocol)) == -1)
        {
            return -1;
        }

        if (will_reuse)
        {
            int yes = 1;
            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
            {
                return -1;
            }
        }

        if (!will_block)
        {
            if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
            {
                return -1;
            }
        }

        return sockfd;
    }

    /*Bind and Listen*/
    int bindAndListen(int sockfd, struct addrinfo *endpoint, int backlog)
    {
        if (bind(sockfd, endpoint->ai_addr, endpoint->ai_addrlen) == -1)
        {
            return -1;
        }
        if (listen(sockfd, backlog) == -1)
        {
            return -1;
        }
        return 0;
    }

    /*Create Epoll File Descriptor*/
    int getEpollInstance()
    {
        int epollfd;
        if ((epollfd = epoll_create1(0)) == -1)
        {
            return -1;
        }
        return epollfd;
    }
    int addEpollInterest(int epollfd, int fd, uint32_t event_mask)
    {
        struct epoll_event ev_hint;
        ev_hint.data.fd = fd;
        ev_hint.events = event_mask;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev_hint) == -1)
        {
            return -1;
        }
        return 0;
    }
    int modifyEpollInterest(int epollfd, int fd, uint32_t event_mask){
        struct epoll_event ev_hint;
        ev_hint.data.fd = fd;
        ev_hint.events = event_mask;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev_hint) == -1)
        {
            return -1;
        }
        return 0;
    }
    int removeEpollInterest(int epollfd, int fd){
        if(epoll_ctl(epollfd, fd, EPOLL_CTL_DEL, NULL) == -1){
            return -1;
        }
        return 0;
    }
    /*returns accepted socket*/
    int acceptConnection(int listen_sock)
    {
        int newfd;
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof(client_addr);

        if ((newfd = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_size)) == -1)
        {
            return -1;
        }

        if (fcntl(newfd, F_SETFL, O_NONBLOCK) == -1)
        {
            return -1;
        }
        char client_ip[INET6_ADDRSTRLEN];
        int client_port;

        if (client_addr.ss_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &ipv4->sin_addr, client_ip, sizeof(client_ip));
            client_port = ntohs(ipv4->sin_port);
        }
        else if (client_addr.ss_family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &ipv6->sin6_addr, client_ip, sizeof(client_ip));
            client_port = ntohs(ipv6->sin6_port);
        }
        else
        {
            std::cerr << "Unknown address family" << std::endl;
            close(newfd);
        }
        // std::cout<<"client ip: "<< client_ip << " client port: "<<client_port<<"\n";
        return newfd;
    }
    /*returns connected socket*/
    int createConnection(int address_family, char *address, char *port)
    {
        struct addrinfo *endpoint = getEndpoint(address_family, false, address, port);
        int sockfd = getSocket(endpoint, false, false);

        if (connect(sockfd, endpoint->ai_addr, endpoint->ai_addrlen) == -1)
        {
            if(errno == EINPROGRESS || errno == EAGAIN){
                return sockfd;
            }
            else{
                return -1;
            }
        }
        return sockfd;
    }
    int closeConnection(int socketfd){
        if(close(socketfd) == -1){
            return -1;
        }
        return 0;
    }
}