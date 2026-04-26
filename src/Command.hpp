#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

#include "DataStore.hpp"

using Tokens = std::vector<std::string>;
using SharedDB = std::shared_ptr<DataStore>;

enum CommandStatus { SUCCESS, BLOCKED, SIGNAL, CLOSE };

struct CommandResult {
    CommandStatus status;
    std::string response;
    std::vector<std::string> keys;
    Clock::time_point timeout;    
    int n_signal;

    CommandResult(std::string res): status{SUCCESS} , response {std::move(res)}, n_signal{1} { }

    CommandResult(CommandStatus s, std::string resp, std::vector<std::string> k = {}, 
            Clock::time_point tp = NO_EXPIRY, int n_sig = 1): status{s}, response{std::move(resp)}, 
            keys{std::move(k)}, timeout{tp}, n_signal{n_sig} { }
};

class Command {
public: 
    virtual CommandResult execute(const Tokens &args) = 0;
    virtual ~Command() = default;
    virtual std::string getTimeoutResponse() const {
        return RESP::encodeNil();
    }
};

class EchoCommand : public Command {
public:
    CommandResult execute(const Tokens& args) override {
        if (args.size() < 2) return RESP::encodeError("wrong number of arguments for 'echo' command");
        return "$" + std::to_string(args[1].size()) + "\r\n" + args[1] + "\r\n";
    }
};

class PingCommand : public Command {
public:
    CommandResult execute(const Tokens& args) override {
        return std::string("+PONG\r\n");
    }
};

class SetCommand : public Command {
public:
    explicit SetCommand(SharedDB db): _db{db} {}

    CommandResult execute(const Tokens& args) override {
        if (args.size() < 3) return RESP::encodeError("wrong number of arguments for 'SET' command");

        Clock::time_point expiry;
        if (args.size() == 3) {
            expiry = Clock::time_point::max();
        } else if (args[3] == "PX" && args.size() == 5) {
            expiry = Clock::now() + std::chrono::milliseconds(std::stoul(args[4]));
        } else if (args[3] == "EX" && args.size() == 5) {
            expiry = Clock::now() + std::chrono::seconds(std::stoul(args[4]));
        } else {
            return RESP::encodeError("wrong number of arguments for 'SET' command");
        }

        _db->set(args[1], args[2], expiry);
        return RESP::encodeOK();
    }

private:
    SharedDB _db;
};

class GetCommand : public Command {
public:
    explicit GetCommand(SharedDB db) : _db{db}{}

    CommandResult execute(const Tokens& args) override {
        if (args.size() < 2) return RESP::encodeError("wrong number of arguments for 'GET' command");
        auto result = _db->get(args[1]);
        if (!result) return RESP::encodeNil();

        auto str_ptr = std::get_if<RedisString>(result);
        if (!str_ptr) return RESP::encodeError("value is not a string");
        return RESP::encodeStr(*str_ptr);
    }

private:
    SharedDB _db;
};

class RPushCommand : public Command {
public:
    RPushCommand(SharedDB db): _db{db} {}

    CommandResult execute(const Tokens& args) override {
        if (args.size() < 3) 
            return {RESP::encodeError("wrong number of arguments for 'RPUSH' command")};

        const RedisKey& key = args[1];
        auto it = _db->get_or_create(key, RedisList{}, NO_EXPIRY);
        auto list_ptr = std::get_if<RedisList>(it);
        if (!list_ptr) // key already exists, but not of type list.
            return {RESP::encodeError("element is not of type list.")};
        list_ptr->insert(list_ptr->end(), args.begin()+2, args.end());
        return {CommandStatus::SIGNAL, RESP::encodeInteger(list_ptr->size()), 
                {key}, NO_EXPIRY, static_cast<int>(args.size())-2};
    }

private:
    SharedDB _db;
};

class LRangeCommand : public Command{
public:
    LRangeCommand(SharedDB db): _db{db} { }

    CommandResult execute(const Tokens& args) override {
        if (args.size() != 4) 
            return RESP::encodeError("wrong number of arguments for 'LRANGE' command");
        
        std::vector<std::string> empty_seq;

        const RedisKey& key = args[1];
        auto ptr = _db->get(key);
        if (!ptr) return RESP::encodeSequence(empty_seq.end(), empty_seq.end());

        auto list_ptr = std::get_if<RedisList>(ptr);
        if (!list_ptr) return RESP::encodeError("element is not of type list.");

        long first = std::stoll(args[2]), last = std::stoll(args[3]);
        long size = list_ptr->size();
        if (last > 0 && last >= size) last = size-1;

        if (last < 0) last = size + last;
        if (last < 0) 
            return RESP::encodeSequence(empty_seq.end(), empty_seq.end());
        if (first < 0) first = size + first; 
        if (first < 0) first = 0;
        if ((first > last) || (first >= size)) // return empty sequence.
            return RESP::encodeSequence(empty_seq.end(), empty_seq.end());
        
        return RESP::encodeSequence(list_ptr->begin()+first, list_ptr->begin()+(last+1));
    }
private:
    SharedDB _db;
};

/**
 * Insert all the specified values at the head of the list stored at key. If key does not exist, 
 * it is created as empty list before performing the push operations. When key holds a value that 
 * is not a list, an error is returned.
 * 
 * It is possible to push multiple elements using a single command call just specifying multiple 
 * arguments at the end of the command. Elements are inserted one after the other to the head of the 
 * list, from the leftmost element to the rightmost element. 
 * So for instance the command LPUSH mylist a b c will result into a list containing c as first element, 
 * b as second element and a as third element.
 * 
 * Syntax: LPUSH key element [element ...]
 * 
 * Response type: Integer
 */
class LPushCommand : public Command {
public:
    LPushCommand(SharedDB db): _db{db} { }

    CommandResult execute(const Tokens& args) override {
        if (args.size() < 3) 
            return {RESP::encodeError("wrong number of arguments for 'LPUSH' command")};

        const RedisKey& key = args[1];
        auto ptr = _db->get_or_create(key, RedisList{}, NO_EXPIRY);

        auto list_ptr = std::get_if<RedisList>(ptr);
        if (!list_ptr) return {RESP::encodeError("value is not of type list")};

        for (size_t i = 2; i < args.size(); i++) {
            list_ptr->emplace_front(args[i]);
        }
        return {CommandStatus::SIGNAL, RESP::encodeInteger(list_ptr->size()), 
            {key}, NO_EXPIRY, static_cast<int>(args.size()-2)};
    }
private:
    SharedDB _db;
};

/**
 * Returns the length of the list stored at key. If key does not exist, it is interpreted as empty list
 * and returns 0. An error is returned when the value stored at key is not a list.
 * 
 * Syntax: LLEN key
 * 
 * Response type: Integer.
 */
class LLenCommand : public Command {
public:
    LLenCommand(SharedDB db): _db{db} { }

    CommandResult execute(const Tokens& args) override {
        if (args.size() != 2) 
            return RESP::encodeError("wrong number of arguments for 'LLEN' command");

        const RedisKey& key = args[1];
        auto ptr = _db->get(key);
        if (!ptr) return RESP::encodeInteger(0);

        auto list_ptr = std::get_if<RedisList>(ptr);
        if (!list_ptr) return RESP::encodeError("value is not of type list");

        return RESP::encodeInteger(static_cast<long>(list_ptr->size()));
    }
private:
    SharedDB _db;
};

/**
 * Removes and returns the first elements of the list stored at key. By default, the command pops a single
 * element from the beginning of the list. When provided with the optional count argument, the reply will 
 * consist of up to count elements, depending on the list's length.
 * 
 * Syntax: LPOP key [count]
 * 
 * Response type: 
 *      Nil (if key does not exist)
 *      Str (when called without the count argument, the value of the first element.)        
 *      Sequence (when called with the count argument, a list of popped elements)
 */
class LPopCommand : public Command {
public:
    LPopCommand(SharedDB db): _db{db} { }

    CommandResult execute(const Tokens& args) override {
        if ((args.size() < 2) || (args.size() > 3)) 
            return RESP::encodeError("wrong number of arguments for 'LPOP' command");

        long n = (args.size() == 3) ? std::stol(args[2]) : 1;
        if (n < 0) return RESP::encodeError("wrong number of arguments for 'LPOP' command");

        const RedisKey& key = args[1];
        auto ptr = _db->get(key);
        if (!ptr || (n == 0)) return RESP::encodeNil();

        auto list_ptr = std::get_if<RedisList>(ptr);
        if (!list_ptr) return RESP::encodeError("value is not of type list");
        if (list_ptr->empty()) return RESP::encodeNil();

        n = std::min(n, static_cast<long>(list_ptr->size()));
        
        if (n == 1) {
            auto response = RESP::encodeStr(list_ptr->front());
            list_ptr->pop_front();
            return CommandResult{response};
        }
        auto response = RESP::encodeSequence(list_ptr->begin(), list_ptr->begin()+n);
        for (size_t i = 0; i < n; i++) {
            list_ptr->pop_front();
        }
        return CommandResult{response};
    }
private:
    SharedDB _db;
};

class BLPopCommand : public Command {
public:
    BLPopCommand(SharedDB db) : db_{db} { }

    CommandResult execute(const Tokens& args) override;
    std::string getTimeoutResponse() const override { return RESP::encodeNilArray(); }
private:
    SharedDB db_;
};

enum CommandType {
    PING,
    ECHO,
    SET,
    GET,
    RPUSH,
    RPOP,
    LRANGE,
    LPUSH,
    LPOP,
    BLPOP,
    LLEN,
    NONE,
};

class CommandRouter {
public:
    explicit CommandRouter(SharedDB db): db_{db} {

        command_table_["PING"] = CommandType::PING;
        command_table_["ECHO"] = CommandType::ECHO;
        command_table_["SET"] = CommandType::SET;
        command_table_["GET"] = CommandType::GET;
        command_table_["RPUSH"] = CommandType::RPUSH;
        command_table_["RPOP"] = CommandType::RPOP;
        command_table_["LRANGE"] = CommandType::LRANGE;
        command_table_["LPUSH"] = CommandType::LPUSH;
        command_table_["LLEN"] = CommandType::LLEN;
        command_table_["LPOP"] = CommandType::LPOP;
        command_table_["BLPOP"] = CommandType::BLPOP;

        routing_table_[command_table_["PING"]] = std::make_unique<PingCommand>();
        routing_table_[command_table_["ECHO"]] = std::make_unique<EchoCommand>();
        routing_table_[command_table_["SET"]] = std::make_unique<SetCommand>(db_);
        routing_table_[command_table_["GET"]] = std::make_unique<GetCommand>(db_);
        routing_table_[command_table_["RPUSH"]] = std::make_unique<RPushCommand>(db_);
        routing_table_[command_table_["LRANGE"]] = std::make_unique<LRangeCommand>(db_);
        routing_table_[command_table_["LPUSH"]] = std::make_unique<LPushCommand>(db_);
        routing_table_[command_table_["LLEN"]] = std::make_unique<LLenCommand>(db_);
        routing_table_[command_table_["LPOP"]] = std::make_unique<LPopCommand>(db_);
        routing_table_[command_table_["BLPOP"]] = std::make_unique<BLPopCommand>(db_);
        
        // TODO; add more later.
    }

    CommandResult routeCommand(const Tokens& tokens) {
        if (tokens.empty()) return RESP::encodeError("empty command");
        
        std::string command_name = tokens[0];
        std::transform(command_name.begin(), command_name.end(), command_name.begin(),
            [](unsigned char c) { return std::toupper(c); });

        auto it = command_table_.find(command_name);
        if (it == command_table_.end()) return "-ERR unknown command '" + command_name + "' \r\n";

        return routing_table_[it->second]->execute(tokens);
    }

    CommandType getCommand(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c);});
        
        auto it = command_table_.find(s);
        if (it == command_table_.end()) return CommandType::NONE;
        return it->second;
    }

    std::string getTimeoutResponse(CommandType cmd_type) {
        return routing_table_[cmd_type]->getTimeoutResponse(); 
    }

private:
    std::unordered_map<CommandType, std::unique_ptr<Command>> routing_table_;
    std::unordered_map<std::string, CommandType> command_table_;
    SharedDB db_;
};