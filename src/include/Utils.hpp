#include <vector>
#include <string>
#include <cstring>
#include <set>
#include <unordered_map>

static std::vector<std::string> parseRESP(const std::string& cmd) {
    if (cmd.empty()) return {};

    size_t current_pos = 1;
    size_t crlf_pos = cmd.find("\r\n");
    size_t array_size = std::stoull(cmd.substr(current_pos, crlf_pos-current_pos));
    current_pos = crlf_pos + 3;  // skip \r\n$
    std::vector<std::string> tokens;
    
    for (size_t i = 0; i < array_size; i++) {
        crlf_pos = cmd.find("\r\n", current_pos);
        size_t element_size = std::stoull(cmd.substr(current_pos, (crlf_pos - current_pos)));
        current_pos = crlf_pos + 2;
        tokens.push_back(cmd.substr(current_pos, element_size));
        current_pos += element_size + 3;
    }
    return tokens;
}

static uint64_t getTimeInMillis() {
    return 0;
}

static uint64_t getTimeInMicros() {
    return 0;
}

/*
    We have routeCommand() in CommandRouter class. That will return string. I'll modify that to return status along
    with the response. This status is an enum and can only be in either of the states SUCCESS, BLOCKED, SIGNAL

    if (SUCCESS) normal operation. No need to do anything.
    if (BLOCKED): Current operation is a waiting for data to come. insert to waiting_queue.
    if (SIGNAL): added some data to the list/sequence. Go and process pending requests in waiting_queue if any.

    IN addition, the main thread maintains a priority_queue of connections that have blocking time for this key
    . When the waiting time exceeded, the main thread should take out the request from the priority_queue and 
    return response to the client with empty string and close the connection.

    When the status was SIGNAL from the routeCommand, the main thread first writes the response to the write buffer
    of current connection then note down about this key. In the next iteration of event loop, the thread first checks
    if any of the keys become ready, and completes those requests that are currently waiting.
*/