// Rooms as the clients see them: where agents (and people) coordinate, with
// a message log and the todos shared there.
#pragma once

#include <json/json.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

struct RoomItem
{
    int64_t id{0};
    std::string name;
    std::string topic;  // "" for none
    int64_t createdAt{0};
    std::vector<std::pair<std::string, int64_t>> members;  // (agent, last seen)
    int64_t cursor{0};  // the newest message's seq, to read or wait after
    int64_t prunedMessages{0};
    int64_t logVersion{0};  // changes when messages are pruned or restored

    static RoomItem fromJson(const Json::Value &v)
    {
        RoomItem r{v["id"].asInt64(), v["name"].asString(),
                   v["topic"].isString() ? v["topic"].asString() : "", v["createdAt"].asInt64()};
        for (const auto &m : v["members"])
            r.members.push_back({m["name"].asString(), m["lastSeen"].asInt64()});
        r.cursor = v["cursor"].asInt64();
        r.prunedMessages = v["prunedMessages"].asInt64();
        r.logVersion = v["logVersion"].asInt64();
        return r;
    }
};

// Ids are per room and get reused once pruned; `seq` orders the log and is
// the cursor for reading what's new.
struct MessageItem
{
    int64_t id{0};
    int64_t seq{0};
    int64_t roomId{0};
    std::string author;  // "" for the server
    std::string text;
    int64_t createdAt{0};
    // An event is written by the server when a room todo changes ("claimed
    // #3 ..."); its text reads after the author's name.
    bool event{false};
    int64_t todoId{0};
    int64_t prunedAt{0};  // pruned messages only

    static MessageItem fromJson(const Json::Value &v)
    {
        return {v["id"].asInt64(),
                v["seq"].asInt64(),
                v["roomId"].asInt64(),
                v["author"].isString() ? v["author"].asString() : "",
                v["text"].asString(),
                v["createdAt"].asInt64(),
                v["kind"].asString() == "event",
                v["todoId"].isNull() ? 0 : v["todoId"].asInt64(),
                v["prunedAt"].asInt64()};
    }
};

namespace rooms
{
// Who we are in rooms: --as, else $TODO_AGENT, else $USER.
inline std::string defaultAgent()
{
    for (const char *var : {"TODO_AGENT", "USER"})
        if (const char *v = std::getenv(var); v && *v)
            return v;
    return "";
}

// "HH:MM" today, else "MM-DD HH:MM".
inline std::string clock(int64_t t)
{
    time_t tt = static_cast<time_t>(t), now = time(nullptr);
    tm a{}, b{};
    localtime_r(&tt, &a);
    localtime_r(&now, &b);
    char buf[32];
    strftime(buf, sizeof(buf),
             a.tm_year == b.tm_year && a.tm_yday == b.tm_yday ? "%H:%M" : "%m-%d %H:%M", &a);
    return buf;
}

// A line of a message cut into plain text and @mentions, so they can be
// colored. A mention is "@name" at the start or after a space or "(", up to the
// next space or "@", without trailing punctuation.
enum class Mention
{
    None,
    Other,
    Me,
};

inline std::vector<std::pair<std::string, Mention>> splitMentions(const std::string &line, const std::string &me)
{
    std::vector<std::pair<std::string, Mention>> out;
    size_t plain = 0, i = 0;
    while (i < line.size())
    {
        const bool start = i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t' || line[i - 1] == '(';
        if (line[i] != '@' || !start)
        {
            ++i;
            continue;
        }
        size_t end = i + 1;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '@')
            ++end;
        while (end > i + 1 && std::string(".,;:!?)").find(line[end - 1]) != std::string::npos)
            --end;
        if (end == i + 1)
        {
            ++i;
            continue;
        }
        if (i > plain)
            out.push_back({line.substr(plain, i - plain), Mention::None});
        const std::string name = line.substr(i + 1, end - i - 1);
        out.push_back({line.substr(i, end - i), !me.empty() && name == me ? Mention::Me : Mention::Other});
        plain = i = end;
    }
    if (plain < line.size())
        out.push_back({line.substr(plain), Mention::None});
    return out;
}

// "just now", "5m ago", "3h ago", "2d ago".
inline std::string ago(int64_t t)
{
    const int64_t d = static_cast<int64_t>(time(nullptr)) - t;
    if (d < 60)
        return "just now";
    if (d < 3600)
        return std::to_string(d / 60) + "m ago";
    if (d < 86400)
        return std::to_string(d / 3600) + "h ago";
    return std::to_string(d / 86400) + "d ago";
}
}  // namespace rooms
