// Claude usage as reported by the collectors (GET /api/usage): one line for
// the UI's status area and a per-device / per-session table.
#pragma once

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace cuse
{
enum class Level { Ok, Warn, Bad, Muted };

struct Seg
{
    std::string text;
    Level level{Level::Muted};
};

inline Level level(double pct) { return pct >= 90 ? Level::Bad : pct >= 70 ? Level::Warn : Level::Ok; }

inline std::string num(double n)
{
    char b[32];
    if (n >= 1e6)
        std::snprintf(b, sizeof b, "%.1fM", n / 1e6);
    else if (n >= 1e3)
        std::snprintf(b, sizeof b, "%.0fk", n / 1e3);
    else
        std::snprintf(b, sizeof b, "%.0f", n);
    return b;
}

// 3h12m, 6d4h, 8m, "now"
inline std::string until(double resetsAt, double now)
{
    long s = static_cast<long>(resetsAt - now);
    char b[32];
    if (s <= 0)
        return "now";
    if (s >= 86400)
        std::snprintf(b, sizeof b, "%ldd%ldh", s / 86400, s % 86400 / 3600);
    else if (s >= 3600)
        std::snprintf(b, sizeof b, "%ldh%02ldm", s / 3600, s % 3600 / 60);
    else
        std::snprintf(b, sizeof b, "%ldm", s / 60);
    return b;
}

inline std::string age(double ts, double now)
{
    long s = static_cast<long>(now - ts);
    if (s < 90)
        return std::to_string(std::max(0L, s)) + "s";
    if (s < 5400)
        return std::to_string(s / 60) + "m";
    return std::to_string(s / 3600) + "h";
}

inline double nowSec() { return static_cast<double>(std::time(nullptr)); }

// The plan limits are the same on every device: use the freshest report that
// has them. Returns null if none.
inline const Json::Value *planSource(const Json::Value &u)
{
    const Json::Value *best = nullptr;
    double bestTs = -1;
    for (const auto &d : u["devices"])
    {
        if (!d["five_hour_pct"].isNumeric())
            continue;
        const double ts = d["status_ts"].isNumeric() ? d["status_ts"].asDouble() : 0;
        if (ts > bestTs)
            best = &d, bestTs = ts;
    }
    return best;
}

// A window's figures are stale once its reset time has passed.
inline Seg limit(const Json::Value &d, const char *label, const char *pct, const char *resets, double now)
{
    if (!d[pct].isNumeric())
        return {std::string(label) + " ?", Level::Muted};
    const double p = d[pct].asDouble();
    char b[48];
    std::snprintf(b, sizeof b, "%s %.0f%%", label, p);
    std::string s = b;
    if (d[resets].isNumeric())
    {
        if (d[resets].asDouble() <= now)
            return {std::string(label) + " ?", Level::Muted};
        if (std::string(label) == "5h")
            s += " (" + until(d[resets].asDouble(), now) + ")";
    }
    return {s, level(p)};
}

// Session names with their summed 5h-window index over all devices, biggest first.
inline std::vector<std::pair<std::string, double>> top(const Json::Value &u)
{
    std::map<std::string, double> sum;
    for (const auto &d : u["devices"])
        for (const auto &s : d["sessions"])
            sum[s["name"].asString()] += s["idx_window"].asDouble();
    std::vector<std::pair<std::string, double>> v(sum.begin(), sum.end());
    std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.second > b.second; });
    return v;
}

// "5h 31% (3h12m) | 7d 4% | top: meera 2.1M, sim 1.0M" as colored pieces.
inline std::vector<Seg> line(const Json::Value &u, double now, size_t maxTop = 2)
{
    std::vector<Seg> out;
    const Json::Value *p = planSource(u);
    if (!p)
        return {{"usage: no data", Level::Muted}};
    out.push_back(limit(*p, "5h", "five_hour_pct", "five_hour_resets_at", now));
    out.push_back({" | ", Level::Muted});
    out.push_back(limit(*p, "7d", "seven_day_pct", "seven_day_resets_at", now));
    auto t = top(u);
    std::string s;
    for (size_t i = 0; i < t.size() && i < maxTop; ++i)
        if (t[i].second > 0)
            s += (s.empty() ? "" : ", ") + t[i].first + " " + num(t[i].second);
    if (!s.empty())
    {
        out.push_back({" | ", Level::Muted});
        out.push_back({"top: " + s, Level::Muted});
    }
    return out;
}

// Plain text rows for a table (also used by the UI popup): the first row is a
// header. Columns are padded to fit.
struct Row
{
    std::vector<std::string> cells;
    Level level{Level::Muted};
    bool header{false};
};

inline std::vector<Row> table(const Json::Value &u, double now)
{
    std::vector<Row> rows;
    rows.push_back({{"device", "name", "project", "model", "30 min", "5h window"}, Level::Muted, true});
    for (const auto &d : u["devices"])
    {
        const double ts = d["ts"].isNumeric() ? d["ts"].asDouble() : 0;
        const bool stale = now - ts > 600;
        double w30 = 0, win = 0;
        for (const auto &s : d["sessions"])
            w30 += s["idx_30m"].asDouble(), win += s["idx_window"].asDouble();
        rows.push_back({{d["device"].asString() + (stale ? " (stale " + age(ts, now) + ")" : ""), "", "", "",
                         num(w30), num(win)},
                        stale ? Level::Bad : Level::Ok});
        for (const auto &s : d["sessions"])
            rows.push_back({{"", s["name"].asString(), s["project"].asString(), s["model"].asString(),
                             num(s["idx_30m"].asDouble()), num(s["idx_window"].asDouble())}});
    }
    return rows;
}

inline std::vector<std::string> layout(const std::vector<Row> &rows, size_t maxProject = 32)
{
    std::vector<size_t> w;
    for (const auto &r : rows)
        for (size_t i = 0; i < r.cells.size(); ++i)
        {
            if (w.size() <= i)
                w.push_back(0);
            // Count code points, not bytes.
            size_t n = 0;
            for (unsigned char ch : r.cells[i])
                n += (ch & 0xC0) != 0x80;
            w[i] = std::max(w[i], i == 2 ? std::min(n, maxProject) : n);
        }
    std::vector<std::string> out;
    for (const auto &r : rows)
    {
        std::string line;
        for (size_t i = 0; i < r.cells.size(); ++i)
        {
            std::string c = r.cells[i];
            size_t n = 0;
            for (unsigned char ch : c)
                n += (ch & 0xC0) != 0x80;
            if (i == 2 && n > maxProject)
            {
                // keep the tail of long paths
                size_t keep = maxProject - 1, cnt = 0, pos = c.size();
                while (pos > 0 && cnt < keep)
                    cnt += (static_cast<unsigned char>(c[--pos]) & 0xC0) != 0x80;
                c = "…" + c.substr(pos);
                n = maxProject;
            }
            const bool right = i >= 4;
            const std::string pad(w[i] > n ? w[i] - n : 0, ' ');
            line += (right ? pad + c : c + pad) + (i + 1 < r.cells.size() ? "  " : "");
        }
        out.push_back(line);
    }
    return out;
}
}  // namespace cuse
