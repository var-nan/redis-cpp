#include "Server.hpp"

void Server::set_non_block(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    flags  |= O_NONBLOCK;
    int rv = fcntl(fd, F_SETFL, flags);
    // TODO : handle error.
}

void Server::acceptConnection() {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int clientfd = accept(fd_, (struct sockaddr *)&client_addr, &addrlen);
    if (clientfd < 0) {
        return ;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    std::cout << std::format("Client Connected: {}.{}.{}.{}\n",(ip&255),((ip>>8)&255),((ip>>16)&255),(ip>>24));

    set_non_block(clientfd);

    std::unique_ptr<Connection> conn = std::make_unique<Connection>(clientfd);
    conn->intent |= WANT_READ;
    // insert to map
    if (clientfd >= connections_.size()) {
        connections_.resize(clientfd + 1);
    }
    connections_[clientfd] = std::move(conn);
}

// Remove all timedout requests from global queue. This doesn't remove the requests from 
// waiting queue. It'll send the timedout response to client and gets removed from the global queue.
// Actual removal takes place when the request is pulled from waiting queue.
void Server::check_global_timeouts() {
    while (!global_timeouts_.empty() && (Clock::now() >= (*(global_timeouts_.cbegin()))->timeout)) {
        auto it= global_timeouts_.begin();
        auto request = *it;
        std::string response = router_.getTimeoutResponse(request->cmd_type);
        request->conn->write_buffer += response;
        request->conn->intent |= WANT_WRITE;
        global_timeouts_.erase(it);
    }
}

void Server::process_ready_keys() {
    // service pending requests for the ready keys and clear the ready keys at the end.
    for (const auto& key: ready_keys_) {
        auto& q = waiting_queue_[key];
        
        // check the waiting queue for this queue and start processing blocked requests
        // until the waiting queue becomes empty or n becomes 0.
        while (!q.empty()) {
            auto blocked_client = q.front().get();
            Connection *conn = blocked_client->conn;

            if (!global_timeouts_.contains(blocked_client)) { // timeout happened.
                q.pop_front();
                continue;
            }
            auto cmd_response = router_.routeCommand(blocked_client->cmd_args, conn->_fd);
            // if the list becomes (by other thread), the current request becomes blocked again,
            // so keep this client in waiting list and exit loop.
            if (cmd_response.status == CommandStatus::BLOCKED) break;
            
            // other status is SUCCESS. These commands should not return SIGNAL.
            conn->write_buffer += cmd_response.response;
            conn->intent |= WANT_WRITE;

            global_timeouts_.erase(blocked_client);
            q.front() = nullptr;
            q.pop_front();
        }
    }
    ready_keys_.clear();
}

void Server::run() {

    std::vector<struct pollfd> pollfds;
    int timeout = -1;
    for ( ; ; ) {

        // service requests from ready keys.
        process_ready_keys();

        // remove timedout requests.
        check_global_timeouts();

        pollfds.clear();
        
        struct pollfd pfd = {fd_, POLLIN, 0};
        pollfds.push_back(pfd); // server socket.
        
        for (const auto &conn : connections_) {
            if (!conn) continue;

            struct pollfd pfd = {conn->_fd, POLLERR, 0};
            pfd.events = (conn->intent & WANT_READ) ? (pfd.events | POLLIN) : pfd.events;
            pfd.events = (conn->intent & WANT_WRITE) ? (pfd.events | POLLOUT) : pfd.events;
            pollfds.push_back(pfd);
        }

        if (!global_timeouts_.empty()) {
            auto first = (*(global_timeouts_.cbegin()))->timeout;
            auto now = Clock::now();
            if (now >= first) continue;
            timeout = std::chrono::duration_cast<std::chrono::milliseconds>(first - now).count();
        }
        // TODO: change to epoll later.
        int nready = poll(pollfds.data(), (nfds_t) pollfds.size(), timeout);
        if (nready < 0 && errno == EINTR) {
            continue;
            // handle error.
        } else if (nready == 0) continue;
        
        // TODO: handle EINTR signal.
        if (pollfds[0].revents) {
            acceptConnection();
        }
        // iterate through all connections
        for (int i = 1; i < pollfds.size(); i++) {
            auto revents = pollfds[i].revents;
            auto& conn = connections_[pollfds[i].fd];
            if (revents & POLLIN) {
                handle_read(conn.get());
            }
            if (revents & POLLOUT) {
                handle_write(conn.get());
            }
            if (revents & (POLLERR | POLLHUP)) {
                conn->intent = WANT_CLOSE;
            }
            if (conn->intent & WANT_CLOSE) {
                handle_disconnect(connections_[pollfds[i].fd].get());
                connections_[pollfds[i].fd] = nullptr;
            }
        }
    }
}

void Server::handle_read(Connection *conn) {
    char buf[BUFFER_SIZE];

    while (true) {
        ssize_t nread = recv(conn->_fd, buf, sizeof(buf), 0);
        if (nread < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            conn->intent = WANT_CLOSE;
            return ;
        } else if (nread == 0) return ;
        conn->appendToReadBuffer(buf, nread);
    }

    size_t cmd_size;
    while ((cmd_size = conn->getCompleteCommandSize()) > 0) {
        std::string cmd = conn->read_buffer.substr(0, cmd_size);
        conn->read_buffer.erase(0, cmd_size);

        auto tokens = parseRESP(cmd);
        auto cmd_response = router_.routeCommand(tokens, conn->_fd);
        // if the command is blocked, put the client to key's waiting queue and add to global timeout queue.
        if (cmd_response.status == CommandStatus::BLOCKED) {
            const RedisKey& key = cmd_response.keys[0];
            
            auto client_ptr = std::make_unique<BlockedClient>(
                BlockedClient{conn, router_.getCommand(tokens.front()), tokens, cmd_response.timeout});
            global_timeouts_.insert(client_ptr.get());
            waiting_queue_[key].push_back(std::move(client_ptr));

            conn->intent = 0; // clear off intention. No need to read additional data from socket.
            // NOTE:: Currently, we only process queries in seqential order. If we process next query before
            // current query completes, that will violate program order (especially important in transactions).
            break;
        } else if (cmd_response.status == CommandStatus::SIGNAL) {
            // signal any blocked clients on this key.
            conn->write_buffer += cmd_response.response;
            ready_keys_.push_back(cmd_response.keys[0]);
        } else conn->write_buffer += cmd_response.response;
    }

    if (!conn->write_buffer.empty())
        conn->intent |= WANT_WRITE;
}

void Server::handle_write(Connection *conn) {
    const char *buf = conn->write_buffer.data();
    size_t buffer_size = conn->write_buffer.size();
    size_t nleft = buffer_size;

    while (nleft > 0) {
        ssize_t nwrite = write(conn->_fd, buf, nleft);
        if (nwrite < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // OS socket buffer is full. Retry later. Keep the intent.
                break;
            } else {
                conn->intent |=  WANT_CLOSE;
                return ;
            }
        }
        buf += nwrite;
        nleft -= nwrite;
    }
    // remove string from beginning
    conn->write_buffer.erase(0, buffer_size-nleft);
    if (nleft != 0){
        conn->intent |= WANT_WRITE;
    }
}

// Since a client can only be blocked in atmost one request. When a client got disconnected, we just need 
// to remove the request from the global queue.
void Server::handle_disconnect(Connection *conn) {
    std::erase_if(global_timeouts_,[conn](const auto& e){ return e->conn == conn; }); // O(N) solution.
}