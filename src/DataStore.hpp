#include <iostream>
#include <unordered_map>
#include <string>
#include <optional>
#include <chrono>

using Clock = std::chrono::steady_clock;

class Value {
public:
    Value() = default;
    explicit Value(const std::string& val, Clock::time_point tp): 
            _value{val}, _expiry{tp} { }

    const std::string& getValue() const { return _value; }
    Clock::time_point getExpiryTime() const { return _expiry; }
private:
    std::string _value;
    Clock::time_point _expiry = Clock::time_point::max();
};

class DataStore {
public:
    DataStore() = default;

    void set(const std::string& key, const std::string& value, Clock::time_point tp) {
        _store[key] = Value{value, tp};
    }

    std::optional<std::string> get(const std::string& key) {
        auto it = _store.find(key);
        if (it == _store.end()) return std::nullopt;
        
        auto expiry_time = it->second.getExpiryTime();
        auto current_time = Clock::now();
        if (current_time > expiry_time) { // delete key
            _store.erase(it);
            return std::nullopt;
        }
        return it->second.getValue();
    }

private:
    using Key = std::string;
    std::unordered_map<Key,Value> _store;
};