#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <netinet/in.h>
#include <errno.h>

#include <unistd.h>
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Too few arguments" << std::endl;
        exit(1);
    }
    int status;
    struct addrinfo hints;
    struct addrinfo *servlist = nullptr; // linked list to all addresses

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(argv[1], argv[2], &hints, &servlist)) != 0)
    {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
        exit(1);
    }

    std::cout << "IP addresses for: " << argv[1] << std::endl;

    struct addrinfo *server_info;

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
            std::cout << ip_string << " " << port << " " << ip_version << std::endl;
            break;
        }
        else
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)server_info->ai_addr;
            addr = &(ipv6->sin6_addr);
            port = ntohs(ipv6->sin6_port);
            ip_version = "IPV6";

            // convert addr to readable format
            inet_ntop(server_info->ai_family, addr, ip_string, sizeof(ip_string));
            std::cout << ip_string << " " << port << " " << ip_version << std::endl;
        }
    }

    

    int sockfd;
    if ((sockfd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol)) == -1)
    {
        std::cerr << "socket error: " << strerror(errno) << std::endl;
        exit(1);
    }

    if (connect(sockfd, server_info->ai_addr, server_info->ai_addrlen) == -1)
    {
        std::cerr << "connect error: " << strerror(errno) << std::endl;
        exit(1);
    }

    freeaddrinfo(servlist);

    char buffer[] = "GET / HTTP/1.1\r\n\r\n";
    int bytes_write;
    bytes_write = send(sockfd, buffer, strlen(buffer), 0);
    std::cout<<bytes_write<<" "<< buffer<<std::endl;
    return 0;
}