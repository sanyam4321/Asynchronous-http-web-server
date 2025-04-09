#pragma once

#include "socket_utilities.h"
#include "thread_safe_map.h"
#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

using Callback = std::function<void(struct epoll_event)>;

namespace FiberConn {
enum EventPriority {
    LISTEN_SOCK = 0,
    NEW_SOCK,
    HELPER_SOCK
};

class IOReactor {
private:
    int epollfd_;
    int max_events_;

    struct epoll_event* events_;

    struct PQComparator {
        bool operator()(const std::pair<EventPriority, struct epoll_event>& a,
            const std::pair<EventPriority, struct epoll_event>& b) const
        {
            return a.first > b.first;
        }
    };

    std::priority_queue<std::pair<EventPriority, struct epoll_event>, std::vector<std::pair<EventPriority, struct epoll_event>>, PQComparator> event_queue_;
    ThreadSafeMap<int, Callback> event_callback_;
    ThreadSafeMap<int, EventPriority> event_priority_;

public:
    IOReactor(int max_events)
    {
        max_events_ = max_events;
        epollfd_ = FiberConn::getEpollInstance();
        events_ = new struct epoll_event[max_events_];
    }
    ~IOReactor()
    {
        delete[] events_;
    }

    int asyncAccept(int listen_sock, Callback cb)
    {
        if (addEpollInterest(epollfd_, listen_sock, EPOLLIN) == -1) {
            std::cerr << "async accept failed\n";
            return -1;
        }
        event_callback_.insert(listen_sock, cb);
        event_priority_.insert(listen_sock, LISTEN_SOCK);
        return 0;
    }

    int addTrack(int sockfd, uint32_t mask, EventPriority priority, Callback cb)
    {
        if (addEpollInterest(epollfd_, sockfd, mask) == -1) {
            std::cerr << "Failed to add new fd to epoll\n";
            return -1;
        }
        event_callback_.insert(sockfd, cb);
        event_priority_.insert(sockfd, priority);
        return 0;
    }
    int modifyTrack(int sockfd, uint32_t mask, EventPriority priority, Callback cb)
    {
        if (modifyEpollInterest(epollfd_, sockfd, mask) == -1) {
            std::cerr << "Failed to modify fd to epoll\n";
            return -1;
        }
        event_callback_.insert(sockfd, cb);
        event_priority_.insert(sockfd, priority);
        return 0;
    }
    int removeTrack(int sockfd)
    {
        if (removeEpollInterest(epollfd_, sockfd) == -1) {
            std::cerr << "Failed to remove fd from epoll\n";
            return -1;
        }
        event_callback_.remove(sockfd);
        event_priority_.remove(sockfd);
        return 0;
    }
    int runCallback(std::pair<EventPriority, struct epoll_event> new_event)
    {
        /*get the callback from the hashmap and run it*/
        int temp_fd = new_event.second.data.fd;
        // std::cout << "Running callback for socket: " << temp_fd << "\n";
        Callback cb;
        if (event_callback_.get(temp_fd, cb)) {
            cb(new_event.second);
            return 0;
        } else {
            std::cerr << "No callback found for fd: " << temp_fd << std::endl;
            return -1;
        }
        return 0;
    }
    int reactorRun()
    {

        while (true) {
            /*Dequeue all the pending event handlers*/
            while (!event_queue_.empty()) {
                /*Run the callbacks*/
                runCallback(event_queue_.top());
                event_queue_.pop();
            }

            /*Wait for new events only if event_callback size is not zero otherwise yield*/
            if (event_callback_.getSize() > 0) {
                /*Epoll wait and push the sockets in the queue*/
                int num_events = epoll_wait(epollfd_, events_, max_events_, -1);
                if (num_events == -1) {
                    std::cerr << "epoll wait error: " << strerror(errno) << std::endl;
                    return -1;
                }

                for (int i = 0; i < num_events; i++) {
                    /*Push all these events to priority queue*/
                    int tempfd = events_[i].data.fd;

                    EventPriority ev_priority = NEW_SOCK;
                    event_priority_.get(tempfd, ev_priority);
                    event_queue_.push(std::make_pair(ev_priority, events_[i]));
                }
            } else {
                /*Dead reactor*/
                // std::cout << "exiting reactor: no more work pending\n";
                return 0;
            }
        }
        return 0;
    }
};
}