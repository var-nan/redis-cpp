#include <vector>
#include <string>
#include <cstring>

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