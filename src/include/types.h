#include <string>
#include <deque>
#include <variant>
#include <set>
#include <array>
#include <functional>
#include <string_view>
#include <numeric>

#include "RedisStream.hpp"

using RedisString = std::string;
using RedisKey = std::string;
using RedisList = std::deque<RedisString>;
using RedisSet = std::set<RedisString>;
using RedisInteger = int64_t;

using RedisType = std::variant<RedisString, RedisList, RedisInteger>;

namespace RESP {
    inline std::string encodeStr(const std::string& data) {
        return "$" + std::to_string(data.size()) + "\r\n" + data + "\r\n";
    }

    template <typename It>
    inline std::string encodeSequence(It first, It last) {
        size_t n_chars = 0, n_elements = 0;
        for (It current = first; current != last; ++current) {
            n_chars += current->size();
            n_elements++;
        }

        // 8 chars per element ($<len>\r\n<element>\r\n)
        size_t space_required = n_chars + (n_elements * 8) + 6;
        std::string result;
        result.reserve(space_required);
        result += "*" + std::to_string(n_elements) + "\r\n";

        for (It current = first; current != last; ++current) {
            result += encodeStr(*current);
        }
        return result;
    }

    template <typename It>
    inline std::string encodeCmdResponses(It first, It last) {

        size_t n_elements = std::distance(first, last);
        size_t n_chars = std::accumulate(first, last, 0, [](size_t c, const std::string& s){
            return c + s.size();
        });

        size_t space_required = n_chars + 6;
        std::string result;
        result.reserve(space_required);
        result = "*" + std::to_string(n_elements) + "\r\n";
        for (It current = first; current != last; ++current) {
            result += *current;
        }
        return result;
    }

    inline std::string encodeError(const std::string& msg) {
        return "-ERR " + msg + "\r\n";
    }

    inline std::string encodeNil() {
        return "$-1\r\n";
    }

    inline std::string encodeNilArray() {
        return "*-1\r\n";
    }

    inline std::string encodeOK() {
        return "+OK\r\n";
    }

    inline std::string encodeSimpleString(const std::string& str) {
        return "+"+str+"\r\n";
    }

    inline std::string encodeInteger(long val) {
        return ":" + std::to_string(val) + "\r\n";
    }

    inline std::string encodeKVPair(const std::string& k, const std::string& val) {
        std::array<std::string, 2> kv_pair = {k, val};
        return encodeSequence(kv_pair.begin(), kv_pair.end());
    }
}