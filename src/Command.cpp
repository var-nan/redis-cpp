#include "Command.hpp"

CommandResult BLPopCommand::execute(const Tokens& args) {
    if (args.size() < 3)
        return {RESP::encodeError("wrong number of arguments for 'BLPOP' command")};

    const RedisKey& key = args[1];
    auto timeout = static_cast<long>(std::stod(args.back()) * 1000);

    auto ptr = db_->get_or_create(key, RedisList{}, NO_EXPIRY);
    auto list_ptr = std::get_if<RedisList>(ptr);
    if (!list_ptr) return {RESP::encodeError("value is not of type list")};

    // if list is empty, return with blocked status.
    Clock::time_point tp = (timeout) ? Clock::now() + std::chrono::milliseconds(timeout) : NO_EXPIRY;
    if (list_ptr->empty()) return {CommandStatus::BLOCKED, "", {key}, tp};

    auto response = RESP::encodeKVPair(key, list_ptr->front());
    list_ptr->pop_front();
    return {response};
}

CommandResult TypeCommand::execute(const Tokens& args) {
    if (args.size() != 2) 
        return {RESP::encodeError("wrong number of arguments for 'TYPE' command")};

    const RedisKey& key = args[1];

    auto ptr = db_->get(key);
    if (!ptr) return {RESP::encodeSimpleString("none")};

    if (std::holds_alternative<RedisString>(*ptr)) return RESP::encodeSimpleString("string");
    return RESP::encodeSimpleString("list");
}

// CommandResult XAddCommand::execute(const Tokens& args) {
//     if (args.size() != 2)
//         return {RESP::encodeError("wrong number of arguments for 'XADD' command")};

//     const RedisString& key = args[1];
//     auto ptr = db_->get_or_create(key, RedisStream{}, NO_EXPIRY);

//     auto stream_ptr = std::get_if<RedisStream>(ptr);
//     if (!stream_ptr) 
//         return {RESP::encodeError("value is not of type stream")};
    
//     // parse the arguements
//     StreamPayload payload;
//     std::string id;
//     stream_ptr->addEntry(id, payload);
// }

CommandResult IncrCommand::execute(const Tokens& args) {
    if (args.size() != 2) 
        return {RESP::encodeError("wrong number of arguments for 'INCR' command")};

    const RedisString& key = args[1];
    auto ptr = db_->get_or_create(key, RedisString{"0"}, NO_EXPIRY);
    
    auto *str_ptr = std::get_if<RedisString>(ptr);

    RedisInteger value;
    const auto *f = str_ptr->data();
    const auto *l = f + str_ptr->size();
    if (!(std::from_chars(f, l, value).ec == std::errc{}))
        return {RESP::encodeError("value is not an integer or out of range")};

    value++;
    *str_ptr = std::to_string(value);
    return {RESP::encodeInteger(value)};
}

CommandResult MultiCommand::executeTxn(TransactionState *state) {

    // if already a transaction?
    if (state->is_active) return {RESP::encodeError("Already transaction")};

    state->is_active = true;
    return {RESP::encodeSimpleString("OK")};
}

CommandResult MultiCommand::execute(const Tokens& args) {
    return {RESP::encodeSimpleString("OK")};
}

CommandResult ExecCommand::execute(const Tokens& args) {
    return {RESP::encodeSimpleString("OK")};
}

CommandResult ExecCommand::executeTxn(TransactionState *state) {
    if (!state || !state->is_active)
        return {RESP::encodeError("EXEC without MULTI")};

    std::vector<std::string> cmd_results;
    cmd_results.reserve(state->command_queue.size());
    
    for (const auto& [cmd, args] : state->command_queue) {
        cmd_results.push_back(std::move(cmd->execute(args).response));
    }
    // clear transaction state
    state->is_active = false;
    state->command_queue.clear();

    return {RESP::encodeSequence(cmd_results.begin(), cmd_results.end())}; 
}

CommandResult DiscardCommand::execute(const Tokens& args) {
    return {RESP::encodeSimpleString("OK")};
}

CommandResult DiscardCommand::executeTxn(TransactionState *state) {
    if (!state->is_active) return {RESP::encodeError("DISCARD without MULTI")};

    state->is_active = false;
    state->command_queue.clear();
    return {RESP::encodeSimpleString("OK")};
}