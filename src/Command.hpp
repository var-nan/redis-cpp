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
        return "";
    }

private:
    SharedDB _db;
};

class GetCommand : public Command {
public:
    explicit GetCommand(SharedDB db) : _db{db}{}

    std::string execute(const Tokens& args) override {
        return "";
    }

private:
    SharedDB _db;
};


class CommandRouter {
public:
    CommandRouter() {
        routing_table["PING"] = std::make_unique<PingCommand>();
        routing_table["ECHO"] = std::make_unique<EchoCommand>();
        
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
};