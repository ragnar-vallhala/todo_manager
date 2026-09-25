// Small helpers shared by the controllers.
#pragma once

#include "TodoStore.h"

#include <drogon/HttpResponse.h>

#include <optional>
#include <string>

namespace http
{
inline drogon::HttpResponsePtr json(const Json::Value &body,
                                    drogon::HttpStatusCode code = drogon::k200OK)
{
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    return resp;
}

inline drogon::HttpResponsePtr error(int code, const std::string &msg)
{
    Json::Value body;
    body["error"] = msg;
    return json(body, static_cast<drogon::HttpStatusCode>(code));
}

inline drogon::HttpResponsePtr error(const StoreError &e)
{
    return error(e.status, e.what());
}

inline std::string trim(const std::string &s)
{
    const char *ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

// Reads an optional id reference such as "categoryId". Absent leaves `out`
// empty; null or 0 means "none" (0). Returns false if the value is invalid.
inline bool parseRef(const Json::Value &body, const char *key, std::optional<int64_t> &out)
{
    if (!body.isMember(key))
        return true;
    const auto &v = body[key];
    if (v.isNull())
        out = 0;
    else if (v.isIntegral() && v.asInt64() >= 0)
        out = v.asInt64();
    else
        return false;
    return true;
}
}  // namespace http
