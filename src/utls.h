#include <poll.h>

struct pollfd {
    int fd; //
    short events; // events interested
    short revents; // events occured
};