#include <string>
#include <deque>
#include <variant>
#include <set>

using RedisString = std::string;
using RedisKey = std::string;
using RedisList = std::deque<RedisString>;
using RedisSet = std::set<RedisString>;

using RedisType = std::variant<RedisString, RedisList>;

namespace RESP {
    inline std::string encodeBulk(const std::string& data) {
        return "$" + std::to_string(data.size()) + "\r\n" + data + "\r\n";
    }

    inline std::string encodeError(const std::string& msg) {
        return "-ERR " + msg + "\r\n";
    }

    inline std::string encodeNil() {
        return "$-1\r\n";
    }

    inline std::string encodeOK() {
        return "+OK\r\n";
    }

    inline std::string encodeInteger(long val) {
        return ":" + std::to_string(val) + "\r\n";
    }
}