// Small helpers shared by the controllers.
#pragma once

#include "TodoStore.h"

#include <drogon/HttpRequest.h>
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

// True if the request asks to include archived categories and their todos
// (`?archived=1`).
inline bool wantsArchived(const drogon::HttpRequestPtr &req)
{
    const auto v = req->getParameter("archived");
    return v == "1" || v == "true";
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
// Who is making the request: `body[key]` if given, else the X-Todo-Agent
// header, else `?agent=`. Names are self-declared; there's no auth.
// Returns "" if none was given; sets `err` if the one given is invalid.
inline std::string agent(const drogon::HttpRequestPtr &req, const Json::Value *body,
                         const char *key, std::string &err)
{
    std::string name;
    if (body && body->isMember(key))
    {
        if (!(*body)[key].isString())
            return err = std::string(key) + " must be a string", "";
        name = (*body)[key].asString();
    }
    else if (!req->getHeader("x-todo-agent").empty())
        name = req->getHeader("x-todo-agent");
    else
        name = req->getParameter("agent");
    name = trim(name);
    if (name.size() > 40 || name.find_first_of("\r\n\t") != std::string::npos)
        return err = "agent names are 1-40 characters on one line", "";
    return name;
}
}  // namespace http
