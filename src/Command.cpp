#include "Command.hpp"

CommandResult BLPopCommand::execute(const Tokens& args) {
    if (args.size() < 3)
        return {RESP::encodeError("wrong number of arguments for 'BLPOP' command")};

    const RedisKey& key = args[1];
    auto timeout = static_cast<long>(std::stod(args.back()) * 1000);

    auto ptr = db_->get(key);
    if (!ptr) return {RESP::encodeNil()};

    auto list_ptr = std::get_if<RedisList>(ptr);
    if (!list_ptr) return {RESP::encodeError("value is not of type list")};

    // if list is empty, return with blocked status.
    Clock::time_point tp = (timeout) ? Clock::now() + std::chrono::milliseconds(timeout) : NO_EXPIRY;
    if (list_ptr->empty()) return {CommandStatus::BLOCKED, "", {key}, tp};

    auto response = RESP::encodeKVPair(key, list_ptr->front());
    list_ptr->pop_front();
    return {response};
}