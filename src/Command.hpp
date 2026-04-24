#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

#include "DataStore.hpp"

using Tokens = std::vector<std::string>;
using SharedDB = std::shared_ptr<DataStore>;

class Command {
public: 
    virtual std::string execute(const Tokens &args) = 0;
    virtual ~Command() = default;
};

class EchoCommand : public Command {
public:
    std::string execute(const Tokens& args) override {
        if (args.size() < 2) return "-ERR wrong number of arguments for 'echo' command \r\n";
        return "$" + std::to_string(args[1].size()) + "\r\n" + args[1] + "\r\n";
    }
};

class PingCommand : public Command {
public:
    std::string execute(const Tokens& args) override {
        return "+PONG\r\n";
    }
};

class SetCommand : public Command {
public:
    explicit SetCommand(SharedDB db): _db{db} {}

    std::string execute(const Tokens& args) override {
        if (args.size() < 3) return "-ERR wrong number of arguments for 'SET' command \r\n";

        Clock::time_point expiry;
        if (args.size() == 3) {
            expiry = Clock::time_point::max();
        } else if (args[3] == "PX" && args.size() == 5) {
            expiry = Clock::now() + std::chrono::milliseconds(std::stoul(args[4]));
        } else if (args[3] == "EX" && args.size() == 5) {
            expiry = Clock::now() + std::chrono::seconds(std::stoul(args[4]));
        } else {
            return "-ERR wrong number of arguments for 'SET' command\r\n";
        }

        _db->set(args[1], args[2], expiry);
        return "+OK\r\n";
    }

private:
    SharedDB _db;
};

class GetCommand : public Command {
public:
    explicit GetCommand(SharedDB db) : _db{db}{}

    std::string execute(const Tokens& args) override {
        if (args.size() < 2) return RESP::encodeError("wrong number of arguments for 'GET' command");
        auto result = _db->get(args[1]);
        if (!result) return RESP::encodeNil();

        auto str_ptr = std::get_if<RedisString>(result);
        if (!str_ptr) return RESP::encodeError("value is not a string");
        return RESP::encodeBulk(*str_ptr);
    }

private:
    SharedDB _db;
};

class RPushCommand : public Command {
public:
    RPushCommand(SharedDB db): _db{db} {}

    std::string execute(const Tokens& args) override {
        if (args.size() < 3) 
            return RESP::encodeError("wrong number of arguments for 'RPUSH' command");

        const RedisKey& key = args[1];
        auto it = _db->get_or_create(key, RedisList{});
        auto list_ptr = std::get_if<RedisList>(it);
        if (!list_ptr) // key already exists, but not of type list.
            return RESP::encodeError("element is not of type list.");
        list_ptr->insert(list_ptr->end(), args.begin()+2, args.end());
        return RESP::encodeInteger(list_ptr->size());
    }

private:
    SharedDB _db;
};

class CommandRouter {
public:
    explicit CommandRouter(SharedDB db): _db{db} {
        routing_table["PING"] = std::make_unique<PingCommand>();
        routing_table["ECHO"] = std::make_unique<EchoCommand>();
        routing_table["SET"] = std::make_unique<SetCommand>(_db);
        routing_table["GET"] = std::make_unique<GetCommand>(_db);
        routing_table["RPUSH"] = std::make_unique<RPushCommand>(_db);
        
        // TODO; add more later.
    }

    std::string routeCommand(const Tokens& tokens) {
        if (tokens.empty()) return "-ERR empty command\r\n";
        
        std::string command_name = tokens[0];
        std::transform(command_name.begin(), command_name.end(), command_name.begin(),
            [](unsigned char c) { return std::toupper(c); });

        auto it = routing_table.find(command_name);
        if (it == routing_table.end()) return "-ERR unknown command '" + command_name + "' \r\n";

        return it->second->execute(tokens);
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Command>> routing_table;
    SharedDB _db;
};