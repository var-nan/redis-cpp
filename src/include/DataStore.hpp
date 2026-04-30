#include <iostream>
#include <unordered_map>
#include <string>
#include <optional>
#include <chrono>
#include <deque>

#include "types.h"

using Clock = std::chrono::steady_clock;

const static Clock::time_point NO_EXPIRY = Clock::time_point::max();

class Value {
public:
    Value() = default;
    explicit Value(RedisType val, Clock::time_point tp): 
            _value{std::move(val)}, _expiry{tp} { }

    RedisType& getValue() { return _value; }
    Clock::time_point getExpiryTime() const { return _expiry; }
private:
    RedisType _value;
    Clock::time_point _expiry;
};

class DataStore {
public:
    DataStore() = default;

    void set(const RedisKey& key, const RedisType& value, Clock::time_point tp = NO_EXPIRY) {
        _store[key] = Value{value, tp};
    }

    RedisType* get(const RedisKey& key) {
        auto it = _store.find(key);
        if (it == _store.end()) return nullptr;
        
        auto expiry_time = it->second.getExpiryTime();
        auto current_time = Clock::now();
        if (current_time > expiry_time) { // delete key
            _store.erase(it);
            return nullptr;
        }
        return &(it->second.getValue());
    }

    const RedisType* get(const RedisKey& key) const {
        return get(key);
    }

    RedisType* get_or_create(const RedisKey& key, RedisType default_val, Clock::time_point tp) {
        auto val_ptr = get(key);
        if (val_ptr) return val_ptr;

        auto [it,inserted] = _store.emplace(key, Value(std::move(default_val), tp));
        return &(it->second.getValue());
    }

private:
    std::unordered_map<RedisKey,Value> _store;
    std::unordered_map<RedisKey, std::pair<ulong, ulong>> ref_counts_;
};