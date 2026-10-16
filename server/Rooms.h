// Validation and JSON shared by the room REST API and the MCP endpoint.
#pragma once

#include "TodoStore.h"

#include <string>

namespace rooms
{
constexpr size_t kMaxTopic = 500;
constexpr size_t kMaxMessage = 10000;
constexpr size_t kDefaultLimit = 100;
constexpr size_t kMaxLimit = 1000;
constexpr double kMaxWait = 300;  // seconds a read may wait for news

// Returns an error message, or "" if `name` is fine.
inline std::string checkName(const std::string &name)
{
    if (name.empty() || name.size() > 60)
        return "room name must be 1-60 characters";
    if (name.find_first_not_of("#0123456789") == std::string::npos)
        return "room name can't be just a number";
    if (name.find_first_of("\r\n\t") != std::string::npos)
        return "room name must be on one line";
    return "";
}

// A room plus its newest message id, the cursor to read or wait after.
inline Json::Value roomJson(const Room &r)
{
    Json::Value v = r.toJson();
    v["lastMessageId"] = static_cast<Json::Int64>(TodoStore::get().lastMessageId(r.id));
    return v;
}
}  // namespace rooms
