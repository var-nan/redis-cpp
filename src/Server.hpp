
#include "Command.hpp"
#include "Utils.hpp"

// socket headers
#include <sys/socket.h>
#include <poll.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#define WANT_READ 1
#define WANT_WRITE 2
#define WANT_CLOSE 4

#define BUFFER_SIZE 1024


class Connection {
public:
    explicit Connection(int client_fd): _fd{client_fd} ,intent{0} {}

    void appendToReadBuffer(const char *data, size_t len) {
        read_buffer.append(data, len);
    }

    size_t getCompleteCommandSize() {
        if (read_buffer.empty() || read_buffer[0] != '*') return false;

        size_t crlf_pos = read_buffer.find("\r\n");
        if (crlf_pos == std::string::npos) return false;

        // length of number string is crlf_pos -1;
        size_t array_size = std::stoull(read_buffer.substr(1, crlf_pos-1));
        size_t current_pos = crlf_pos + 2; // skip "\r\n$"

        size_t i = 0;
        for ( ; (i < array_size) && (current_pos < read_buffer.size()); i++) {
            if (read_buffer[current_pos] == '$') current_pos++;
            else return false;

            if((crlf_pos = read_buffer.find("\r\n", current_pos)) == std::string::npos)
                return false;
            
            size_t element_size = std::stoull(read_buffer.substr(current_pos, crlf_pos - current_pos));
            current_pos = crlf_pos + 2 + element_size + 2; // skip three characters "\r\n"
            
            if (current_pos > read_buffer.size()) return false;
        }
        return current_pos;
    }

    ~Connection() {
        close(_fd);
    }

    int _fd;
    uint32_t intent;
    std::string read_buffer;
    std::string write_buffer;
};

class Server {
public:
    Server(int port) : router{std::make_shared<DataStore>()}{

        if((_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            std::cerr << "Failed to create server socket\n";
            return ;
        }
        int reuse = 1;
        if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            std::cerr << "setsockopt failed\n";
            return ;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (bind(_fd, (const struct sockaddr *)&addr, sizeof(addr))) {
            std::cerr << "bind()" << std::endl;
            return ;
        }

        if (listen(_fd, SOMAXCONN)) {
            std::cerr << "listen() " << std::endl;
            return ;
        }
        set_non_block(_fd);
        // server ready to accept connections.
    }

    void run() {

        std::vector<struct pollfd> pollfds;
        int timeout = -1;
        for ( ; ; ) {

            pollfds.clear();
            
            struct pollfd pfd = {_fd, POLLIN, 0};
            pollfds.push_back(pfd); // server socket.
            
            for (const auto &conn : connections) {
                if (!conn) continue;

                struct pollfd pfd = {conn->_fd, POLLERR, 0};
                pfd.events = (conn->intent & WANT_READ) ? (pfd.events | POLLIN) : pfd.events;
                pfd.events = (conn->intent & WANT_WRITE) ? (pfd.events | POLLOUT) : pfd.events;
                pollfds.push_back(pfd);
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
                auto& conn = connections[pollfds[i].fd];
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
                    connections[pollfds[i].fd] = nullptr;
                }
            }
        }
    }

private:
    int _fd;
    std::vector<std::unique_ptr<Connection>> connections;
    CommandRouter router;

    void acceptConnection() {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int clientfd = accept(_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (clientfd < 0) {
            return ;
        }
        uint32_t ip = client_addr.sin_addr.s_addr;
        std::cout << "Client Connected: " <<
            (ip&255) << "." << ((ip>>8)&255) << "." << ((ip>>16)&255) << "." << (ip>>24) 
            << std::endl;

        set_non_block(clientfd);

        std::unique_ptr<Connection> conn = std::make_unique<Connection>(clientfd);
        conn->intent |= WANT_READ;
        // insert to map
        if (clientfd >= connections.size()) {
            connections.resize(clientfd + 1);
        }
        connections[clientfd] = std::move(conn);
    }

    void set_non_block(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        flags  |= O_NONBLOCK;
        int rv = fcntl(fd, F_SETFL, flags);
        // TODO : handle error.
    }

    void handle_read(Connection *conn) {
        char buf[BUFFER_SIZE];

        while (true) {
            ssize_t nread = recv(conn->_fd, buf, sizeof(buf), 0);
            if (nread < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                conn->intent = WANT_CLOSE;
                return ;
            } else if (nread == 0) {
                return ;
            }
            conn->appendToReadBuffer(buf, nread);
        }

        size_t cmd_size;
        while ((cmd_size = conn->getCompleteCommandSize()) > 0) {
            std::string cmd = conn->read_buffer.substr(0, cmd_size);
            conn->read_buffer.erase(0, cmd_size);

            auto tokens = parseRESP(cmd);
            std::string response = router.routeCommand(tokens);
            conn->write_buffer += response;
        }

        if (!conn->write_buffer.empty())
            conn->intent |= WANT_WRITE;
    }

    void handle_write(Connection *conn) {
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
};