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
        routing_table_[command_table_["TYPE"]] = std::make_unique<TypeCommand>(db_);
        routing_table_[command_table_["INCR"]] = std::make_unique<IncrCommand>(db_);
        routing_table_[command_table_["MULTI"]] = std::make_unique<MultiCommand>(db_);
        routing_table_[command_table_["EXEC"]] = std::make_unique<ExecCommand>(db_);
        routing_table_[command_table_["DISCARD"]] = std::make_unique<DiscardCommand>(db_);
        
        // TODO; add more later.
    }

    CommandResult routeCommand(const Tokens& tokens, int fd) {
        if (tokens.empty()) return RESP::encodeError("empty command");
        
        std::string command_name = tokens[0];
        std::transform(command_name.begin(), command_name.end(), command_name.begin(),
            [](unsigned char c) { return std::toupper(c); });

        auto it = command_table_.find(command_name);
        if (it == command_table_.end()) return "-ERR unknown command '" + command_name + "' \r\n";

        if (transactions_.size() <= fd) {
            transactions_.reserve(fd+1);
            for (int i = transactions_.size(); i < (fd+1); i++) {
                transactions_.emplace_back(std::make_unique<TransactionState>());
            }
        }
        auto cmd_ptr = routing_table_[it->second].get();
        if (it->second == CommandType::MULTI) {
            auto ptr = dynamic_cast<MultiCommand *>(cmd_ptr);
            return ptr->executeTxn(transactions_[fd].get());
        } else if (it->second == CommandType::EXEC) {
            auto ptr = dynamic_cast<ExecCommand *>(routing_table_[it->second].get());
            return ptr->executeTxn(transactions_[fd].get());
        } else if (it->second == CommandType::DISCARD) {
            auto ptr = dynamic_cast<DiscardCommand *>(cmd_ptr);
            return ptr->executeTxn(transactions_[fd].get());
        } else if (transactions_[fd]->is_active) {
            // No need to validate command arguments.
            transactions_[fd]->command_queue.emplace_back(routing_table_[it->second].get(), tokens);
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
        transactions_[idx]->is_active = false;
        transactions_[idx]->command_queue.clear();
    }

private:
    std::unordered_map<CommandType, std::unique_ptr<Command>> routing_table_;
    std::unordered_map<std::string, CommandType> command_table_;
    std::vector<std::unique_ptr<TransactionState>> transactions_;
    SharedDB db_;
};