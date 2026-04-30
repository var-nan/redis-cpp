#include "Command.hpp"

CommandResult SetCommand::execute(const Tokens& args) {
    if (args.size() < 3) return RESP::encodeError("wrong number of arguments for 'SET' command");

    Clock::time_point expiry;
    if (args.size() == 3) {
        expiry = Clock::time_point::max();
    } else if (args[3] == "PX" && args.size() == 5) {
        expiry = Clock::now() + std::chrono::milliseconds(std::stoul(args[4]));
    } else if (args[3] == "EX" && args.size() == 5) {
        expiry = Clock::now() + std::chrono::seconds(std::stoul(args[4]));
    } else {
        return {RESP::encodeError("wrong number of arguments for 'SET' command")};
    }

    _db->set(args[1], args[2], expiry);

    // update the epoch counter in context
    auto& watched_keys = ctx_->watched_keys;
    if (auto it = watched_keys.find(args[1]); it != watched_keys.end()) {
        auto& e = it->second;
        e.updated_at++;
    } // if not present, no one is watching this key.
    return {RESP::encodeOK()};
}
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

CommandResult MultiCommand::execute(const Tokens& args) {
    auto state = ctx_->transactions[ctx_->fd].get();
    // if already a transaction?
    if (state->is_active) return {RESP::encodeError("Already transaction")};

    state->is_active = true;
    return {RESP::encodeSimpleString("OK")};
}

CommandResult ExecCommand::execute(const Tokens& args) {
    auto state = ctx_->transactions[ctx_->fd].get();
    if (!state || !state->is_active)
        return {RESP::encodeError("EXEC without MULTI")};

    // make sure all the watched keys are not modified.
    int fd = ctx_->fd;
    auto& watched_keys = ctx_->transactions[fd]->watched_keys;

    for (const auto& [k,ts] : watched_keys) {
        const auto& e = ctx_->watched_keys[k];
        // k must exist in the the watched_keys.
        if (e.updated_at > ts) {
            // abort transaction. unwatch all keys and clear state.
            ctx_->clearWatchedKeys(ctx_->transactions[ctx_->fd]->watched_keys);
            state->clear();
            return {RESP::encodeNilArray()};
        } 
    }

    std::vector<std::string> cmd_results;
    cmd_results.reserve(state->command_queue.size());
    
    for (const auto& [cmd, args] : state->command_queue) {
        cmd_results.push_back(std::move(cmd->execute(args).response));
    }
    state->clear(); // clear transaction state.
    return {RESP::encodeCmdResponses(cmd_results.begin(), cmd_results.end())}; 
}

CommandResult DiscardCommand::execute(const Tokens& args) {
    auto state = ctx_->transactions[ctx_->fd].get();
    if (!state->is_active) return {RESP::encodeError("DISCARD without MULTI")};

    ctx_->clearWatchedKeys(ctx_->transactions[ctx_->fd]->watched_keys);
    state->clear();
    return {RESP::encodeSimpleString("OK")};
}

CommandResult WatchCommand::execute(const Tokens& args) {
    if (args.size() < 2) 
        return {RESP::encodeError("wrong number of arguments for 'WATCH' command")};
    
    int fd = ctx_->fd;
    auto& counts_map = ctx_->watched_keys;
    // for all the given keys, insert them to client's watch list.
    for (auto current = args.cbegin()+1; current != args.cend(); ++current) {
        auto it = counts_map.find(*current);
        ulong ts = 0;
        // increase reference count and insert to client's watch list.
        // if key not present, insert to watch list. If other request sets the key, it should start with 1.
        if (it != counts_map.end()){ // if key not present, insert to watch list.
            auto& e = it->second;
            e.ref_count++;
            ts = e.updated_at;
        }
        else counts_map.insert(std::make_pair(*current, ValueEpoch{1,0}));
        ctx_->transactions[fd]->watched_keys.push_back(std::make_pair(*current, ts));
    }
    return {RESP::encodeSimpleString("OK")};
}

CommandResult UnwatchCommand::execute(const Tokens& args) {
    
    int fd = ctx_->fd;
    auto& client_watched_keys = ctx_->transactions[fd]->watched_keys;
    for (const auto& [k,_] : client_watched_keys) {
        auto& counts_map = ctx_->watched_keys;
        if (auto it = counts_map.find(k); it != counts_map.end()) {
            auto e = it->second;
            e.ref_count--;
            if (e.ref_count == 0){ // no one is referencing this key.
                counts_map.erase(it);
            }
        }
    }
    ctx_->clearWatchedKeys(client_watched_keys); // update context.
    client_watched_keys.clear(); // only clear watched keys.
    return {RESP::encodeSimpleString("OK")};
}