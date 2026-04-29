
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

struct BlockedClient {
    Connection *conn;
    CommandType cmd_type;
    std::vector<std::string> cmd_args; // command arguments 
    Clock::time_point timeout;

    BlockedClient (Connection *c, CommandType cmd, std::vector<std::string> args, 
        Clock::time_point tp = NO_EXPIRY): conn{c}, cmd_type{cmd} , 
        cmd_args{std::move(args)} , timeout{tp} {}
};

struct ConnectionTimeoutComparator {
    bool operator()(const BlockedClient *a, const BlockedClient *b) const {
        return (a->timeout == b->timeout) ? (a->conn->_fd < b->conn->_fd) : (a->timeout < b->timeout);
    }
};

class Server {
public:
    Server(int port) : db_{std::make_unique<DataStore>()}, router_{db_.get()} {

        if((fd_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            std::cerr << "Failed to create server socket\n";
            return ;
        }
        int reuse = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            std::cerr << "setsockopt failed\n";
            return ;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (bind(fd_, (const struct sockaddr *)&addr, sizeof(addr))) {
            std::cerr << "bind()" << std::endl;
            return ;
        }

        if (listen(fd_, SOMAXCONN)) {
            std::cerr << "listen() " << std::endl;
            return ;
        }
        set_non_block(fd_);
        // server ready to accept connections.
    }

    void run(); // event-loop on the connections.

private:
    std::unique_ptr<DataStore> db_;
    int fd_;
    std::vector<std::unique_ptr<Connection>> connections_;
    CommandRouter router_;
    std::unordered_map<std::string, std::deque<std::unique_ptr<BlockedClient>>> waiting_queue_;
    std::set<BlockedClient *, ConnectionTimeoutComparator> global_timeouts_;
    std::deque<std::string> ready_keys_;

    void acceptConnection();
    void set_non_block(int fd);
    void handle_read(Connection *conn);
    void handle_write(Connection *conn); 
    void process_ready_keys();
    void check_global_timeouts();
    void handle_disconnect(Connection *conn);
};