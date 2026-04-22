#include <iostream>
#include <unordered_map>
#include <string>
#include <optional>


class DataStore {
public:
    DataStore() = default;

    void set(const std::string& key, const std::string& value) {
        _store[key] = value;
    }

    std::optional<std::string> get(const std::string& key) {
        auto it = _store.find(key);
        if (it == _store.end()) return std::nullopt;
        return it->second;
    }

private:
    using Key = std::string;
    using Value = std::string;
    std::unordered_map<Key,Value> _store;
};