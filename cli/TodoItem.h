// A todo as the clients see it, plus priority / deadline helpers shared by
// the CLI and the TUI.
#pragma once

#include <json/json.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

struct TodoItem
{
    int64_t id{0};
    std::string title;
    bool done{false};
    int64_t createdAt{0};
    int64_t categoryId{0};  // 0 = no category (Inbox)
    int priority{0};        // 0 none, 1 low, 2 medium, 3 high
    std::string deadline;   // "YYYY-MM-DD", or "" for none
    std::string description;  // optional notes, may span lines
    int64_t prunedAt{0};    // set on pruned todos, whose ids are their own (p1, p2...)

    bool pruned() const { return prunedAt != 0; }
    std::string idLabel() const { return (pruned() ? "p" : "") + std::to_string(id); }

    static TodoItem fromJson(const Json::Value &v);
};

namespace todo
{
constexpr const char *kPriorities[] = {"", "low", "medium", "high"};

inline std::string priorityName(int p)
{
    return p >= 1 && p <= 3 ? kPriorities[p] : "";
}

// "high"/"h"/"3" and so on; "none"/"-"/"0" is 0. Returns nullopt otherwise.
inline std::optional<int> parsePriority(const std::string &s)
{
    if (s == "none" || s == "-" || s == "0")
        return 0;
    for (int i = 1; i <= 3; ++i)
        if (s == kPriorities[i] || s == std::string(1, kPriorities[i][0]) ||
            s == std::to_string(i))
            return i;
    return std::nullopt;
}

// "!!!", "!!", "!" or "".
inline std::string priorityMarker(int p)
{
    return std::string(p >= 1 && p <= 3 ? p : 0, '!');
}

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's algorithm).
inline int64_t daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline std::string civilFromDays(int64_t z)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int y = static_cast<int>(yoe + era * 400 + (m <= 2));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return buf;
}

// Day number of a valid "YYYY-MM-DD"; nullopt if it isn't one.
inline std::optional<int64_t> dayNumber(const std::string &date)
{
    int y, m, d;
    char extra;
    if (date.size() != 10 || std::sscanf(date.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3 ||
        m < 1 || m > 12 || d < 1 || d > 31)
        return std::nullopt;
    const int64_t n = daysFromCivil(y, m, d);
    if (civilFromDays(n) != date)  // e.g. 2026-02-30
        return std::nullopt;
    return n;
}

inline int64_t today()
{
    time_t now = time(nullptr);
    tm t{};
    localtime_r(&now, &t);
    return daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

// 0 = Sunday.
inline int weekday(int64_t day)
{
    return static_cast<int>(((day % 7) + 11) % 7);
}

constexpr const char *kWeekdays[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

// What people type for a deadline: YYYY-MM-DD, today, tomorrow, +N (days
// from now), or a weekday (the next one, never today). "none"/"" clears it
// and gives "". Returns nullopt if it can't be read.
inline std::optional<std::string> parseDeadline(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    if (s.empty() || s == "none" || s == "-")
        return std::string();
    const int64_t now = today();
    if (s == "today")
        return civilFromDays(now);
    if (s == "tomorrow" || s == "tmr")
        return civilFromDays(now + 1);
    if (s[0] == '+' && s.size() > 1 && s.size() < 6 &&
        s.find_first_not_of("0123456789", 1) == std::string::npos)
        return civilFromDays(now + std::stoi(s.substr(1)));
    static const char *fullNames[] = {"sunday",   "monday", "tuesday", "wednesday",
                                      "thursday", "friday", "saturday"};
    for (int i = 0; i < 7; ++i)
        if (s == kWeekdays[i] || s == fullNames[i])
        {
            int ahead = (i - weekday(now) + 7) % 7;
            return civilFromDays(now + (ahead ? ahead : 7));
        }
    if (dayNumber(s))
        return s;
    return std::nullopt;
}

// Short and relative when near: "today", "tomorrow", "Fri", "3d ago",
// "Oct 5", or the date itself further out.
inline std::string dueLabel(const std::string &deadline)
{
    auto day = dayNumber(deadline);
    if (!day)
        return deadline;
    const int64_t diff = *day - today();
    if (diff == 0)
        return "today";
    if (diff == 1)
        return "tomorrow";
    if (diff == -1)
        return "yesterday";
    if (diff < 0)
        return std::to_string(-diff) + "d ago";
    if (diff < 7)
    {
        std::string w = kWeekdays[weekday(*day)];
        w[0] = static_cast<char>(w[0] - 'a' + 'A');
        return w;
    }
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int month = std::stoi(deadline.substr(5, 2));
    const std::string label = std::string(months[month - 1]) + " " +
                              std::to_string(std::stoi(deadline.substr(8, 2)));
    return deadline.substr(0, 4) == civilFromDays(today()).substr(0, 4)
               ? label
               : label + " " + deadline.substr(0, 4);
}

// Past its deadline and not done.
inline bool overdue(const TodoItem &t)
{
    auto day = dayNumber(t.deadline);
    return !t.done && day && *day < today();
}

inline bool dueToday(const TodoItem &t)
{
    auto day = dayNumber(t.deadline);
    return !t.done && day && *day == today();
}

// List order: higher priority first, then earlier deadline (none last), then
// oldest first. Done todos keep their place, so checking one off doesn't
// move it.
inline bool listOrder(const TodoItem &a, const TodoItem &b)
{
    if (a.priority != b.priority)
        return a.priority > b.priority;
    if (a.deadline != b.deadline)
    {
        if (a.deadline.empty() || b.deadline.empty())
            return b.deadline.empty();
        return a.deadline < b.deadline;
    }
    return a.id < b.id;
}
}  // namespace todo

inline TodoItem TodoItem::fromJson(const Json::Value &v)
{
    TodoItem t{v["id"].asInt64(), v["title"].asString(), v["done"].asBool(),
               v["createdAt"].asInt64(),
               v["categoryId"].isNull() ? 0 : v["categoryId"].asInt64()};
    t.priority = todo::parsePriority(v["priority"].asString()).value_or(0);
    t.deadline = v["deadline"].isString() ? v["deadline"].asString() : "";
    t.description = v["description"].isString() ? v["description"].asString() : "";
    t.prunedAt = v["prunedAt"].asInt64();
    return t;
}
