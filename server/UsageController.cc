#include "UsageController.h"
#include "HttpUtil.h"

#include <chrono>
#include <map>
#include <mutex>

using http::json;

namespace
{
std::mutex gMutex;
std::map<std::string, Json::Value> gReports;  // device -> latest report

int64_t nowSec()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string str(const Json::Value &v, const char *key, size_t max)
{
    if (!v.isObject() || !v[key].isString())
        return "";
    auto s = v[key].asString();
    s = http::trim(s);
    if (s.size() > max)
        s.resize(max);
    return s;
}

// Copies the numeric field `key` if present.
void num(const Json::Value &in, Json::Value &out, const char *key)
{
    if (in.isObject() && in[key].isNumeric())
        out[key] = in[key].asDouble();
}
}  // namespace

// GET /api/usage  {"now", "devices": [report + "received"]}, newest report first.
void UsageController::list(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value out;
    out["now"] = static_cast<Json::Int64>(nowSec());
    out["devices"] = Json::Value(Json::arrayValue);
    std::lock_guard<std::mutex> lock(gMutex);
    for (const auto &[name, r] : gReports)
        out["devices"].append(r);
    cb(json(out));
}

// POST /api/usage  {device, five_hour_pct, five_hour_resets_at, seven_day_pct,
// seven_day_resets_at, model, status_ts, ts, sessions: [{name, project, model,
// idx_30m, idx_window}]}. Replaces the device's previous report.
void UsageController::report(const HttpRequestPtr &req, Callback &&cb)
{
    const auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return cb(http::error(400, "expected a JSON object"));
    Json::Value r;
    r["device"] = str(*body, "device", 40);
    if (r["device"].asString().empty())
        return cb(http::error(400, "device is required"));
    for (auto k : {"five_hour_pct", "five_hour_resets_at", "seven_day_pct", "seven_day_resets_at",
                   "status_ts", "ts"})
        num(*body, r, k);
    r["model"] = str(*body, "model", 60);
    r["sessions"] = Json::Value(Json::arrayValue);
    const auto &ss = (*body)["sessions"];
    if (ss.isArray())
        for (Json::ArrayIndex i = 0; i < ss.size() && i < 200; ++i)
        {
            Json::Value s;
            s["name"] = str(ss[i], "name", 40);
            s["project"] = str(ss[i], "project", 120);
            s["model"] = str(ss[i], "model", 60);
            num(ss[i], s, "idx_30m");
            num(ss[i], s, "idx_window");
            r["sessions"].append(s);
        }
    r["received"] = static_cast<Json::Int64>(nowSec());
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gReports.size() >= 32 && !gReports.count(r["device"].asString()))
            return cb(http::error(400, "too many devices"));
        gReports[r["device"].asString()] = r;
    }
    Json::Value ok;
    ok["ok"] = true;
    cb(json(ok));
}
