#include "Command.hpp"

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
        command_table_["TYPE"] = CommandType::TYPE;
        command_table_["MULTI"] = CommandType::MULTI;
        command_table_["INCR"] = CommandType::INCR;
        command_table_["EXEC"] = CommandType::EXEC;
        command_table_["DISCARD"] = CommandType::DISCARD;
        command_table_["WATCH"] = CommandType::WATCH;
        command_table_["UNWATCH"] = CommandType::UNWATCH;

        routing_table_[command_table_["PING"]] = std::make_unique<PingCommand>();
        routing_table_[command_table_["ECHO"]] = std::make_unique<EchoCommand>();
        routing_table_[command_table_["SET"]] = std::make_unique<SetCommand>(db_, &ctx_);
        routing_table_[command_table_["GET"]] = std::make_unique<GetCommand>(db_);
        routing_table_[command_table_["RPUSH"]] = std::make_unique<RPushCommand>(db_);
        routing_table_[command_table_["LRANGE"]] = std::make_unique<LRangeCommand>(db_);
        routing_table_[command_table_["LPUSH"]] = std::make_unique<LPushCommand>(db_);
        routing_table_[command_table_["LLEN"]] = std::make_unique<LLenCommand>(db_);
        routing_table_[command_table_["LPOP"]] = std::make_unique<LPopCommand>(db_);
        routing_table_[command_table_["BLPOP"]] = std::make_unique<BLPopCommand>(db_);
        routing_table_[command_table_["TYPE"]] = std::make_unique<TypeCommand>(db_);
        routing_table_[command_table_["INCR"]] = std::make_unique<IncrCommand>(db_);
        routing_table_[command_table_["MULTI"]] = std::make_unique<MultiCommand>(db_, &ctx_);
        routing_table_[command_table_["EXEC"]] = std::make_unique<ExecCommand>(db_, &ctx_);
        routing_table_[command_table_["DISCARD"]] = std::make_unique<DiscardCommand>(db_, &ctx_);
        routing_table_[command_table_["WATCH"]] = std::make_unique<WatchCommand>(db_, &ctx_);
        routing_table_[command_table_["UNWATCH"]] = std::make_unique<UnwatchCommand>(db_, &ctx_);
        
        // TODO; add more later.
    }

    CommandResult routeCommand(const Tokens& tokens, int fd) {
        if (tokens.empty()) return RESP::encodeError("empty command");
        
        std::string command_name = tokens[0];
        std::transform(command_name.begin(), command_name.end(), command_name.begin(),
            [](unsigned char c) { return std::toupper(c); });

        auto it = command_table_.find(command_name);
        if (it == command_table_.end()) return "-ERR unknown command '" + command_name + "' \r\n";

        if (ctx_.transactions.size() <= fd) {
            ctx_.transactions.reserve(fd+1);
            for (int i = ctx_.transactions.size(); i < (fd+1); i++) {
                ctx_.transactions.emplace_back(std::make_unique<TransactionState>());
            }
        }
        auto cmd_ptr = routing_table_[it->second].get();
        ctx_.fd = fd;
        if (it->second == CommandType::MULTI || 
            it->second == CommandType::EXEC  || 
            it->second == CommandType::DISCARD) {
            return cmd_ptr->execute(tokens);
        } else if (ctx_.transactions[fd]->is_active) {
            if (it->second == CommandType::WATCH)
                return {RESP::encodeError("WATCH inside MULTI is not allowed")};
            ctx_.transactions[fd]->command_queue.emplace_back(cmd_ptr, tokens);
            return {RESP::encodeSimpleString("QUEUED")};
        }
        return cmd_ptr->execute(tokens);
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

    void clearTxn(int idx) {
        ctx_.transactions[ctx_.fd]->is_active = false;
        ctx_.transactions[ctx_.fd]->command_queue.clear();
        // transactions_[idx]->is_active = false;
        // transactions_[idx]->command_queue.clear();
    }

private:
    std::unordered_map<CommandType, std::unique_ptr<Command>> routing_table_;
    std::unordered_map<std::string, CommandType> command_table_;
    
    TransactionContext ctx_;
    // key -> pair of ref-count, epoch count.
    SharedDB db_;
};