#include "TodoController.h"
#include "HttpUtil.h"
#include "TodoStore.h"

using http::error;
using http::json;

namespace
{
constexpr size_t kMaxTitle = 500;
}  // namespace

// GET /api/todos[?status=all|pending|done]
void TodoController::list(const HttpRequestPtr &req, Callback &&cb)
{
    const auto status = req->getParameter("status");
    Json::Value arr(Json::arrayValue);
    for (const auto &t : TodoStore::get().list())
    {
        if ((status == "pending" && t.done) || (status == "done" && !t.done))
            continue;
        arr.append(t.toJson());
    }
    cb(json(arr));
}

// POST /api/todos  {"title": "...", "categoryId"?: id|null}
void TodoController::create(const HttpRequestPtr &req, Callback &&cb)
{
    auto body = req->getJsonObject();
    if (!body || !(*body)["title"].isString())
        return cb(error(400, "body must be JSON with a \"title\" string"));

    auto title = http::trim((*body)["title"].asString());
    if (title.empty() || title.size() > kMaxTitle)
        return cb(error(400, "title must be 1-500 characters"));

    std::optional<int64_t> categoryId;
    if (!http::parseRef(*body, "categoryId", categoryId))
        return cb(error(400, "categoryId must be an id or null"));

    try
    {
        cb(json(TodoStore::get().add(title, categoryId.value_or(0)).toJson(), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// PATCH /api/todos/{id}  {"title"?: "...", "done"?: bool, "categoryId"?: id|null}
void TodoController::update(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return cb(error(400, "body must be a JSON object"));

    std::optional<std::string> title;
    std::optional<bool> done;
    std::optional<int64_t> categoryId;
    if (body->isMember("title"))
    {
        if (!(*body)["title"].isString())
            return cb(error(400, "title must be a string"));
        title = http::trim((*body)["title"].asString());
        if (title->empty() || title->size() > kMaxTitle)
            return cb(error(400, "title must be 1-500 characters"));
    }
    if (body->isMember("done"))
    {
        if (!(*body)["done"].isBool())
            return cb(error(400, "done must be a boolean"));
        done = (*body)["done"].asBool();
    }
    if (!http::parseRef(*body, "categoryId", categoryId))
        return cb(error(400, "categoryId must be an id or null"));
    if (!title && !done && !categoryId)
        return cb(error(400, "nothing to update"));

    try
    {
        auto t = TodoStore::get().update(id, title, done, categoryId);
        if (!t)
            return cb(error(404, "todo not found"));
        cb(json(t->toJson()));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// DELETE /api/todos/{id}
void TodoController::remove(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    if (!TodoStore::get().remove(id))
        return cb(error(404, "todo not found"));
    cb(HttpResponse::newHttpResponse(k204NoContent, CT_NONE));
}

// DELETE /api/todos/done  -- remove all completed todos
void TodoController::clearDone(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value body;
    body["removed"] = static_cast<Json::UInt64>(TodoStore::get().clearDone());
    cb(json(body));
}
