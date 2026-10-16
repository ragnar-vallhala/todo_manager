#include "TodoController.h"
#include "Files.h"
#include "HttpUtil.h"
#include "TodoStore.h"

#include <cstdlib>

using http::error;
using http::json;

namespace
{
constexpr size_t kMaxTitle = 500;
constexpr size_t kMaxDescription = 10000;

// Reads optional "priority" ("low"|"medium"|"high"|null), "deadline"
// ("YYYY-MM-DD"|null), "description" (text|null) and "attachments" (a list
// of uploaded files' {"id", "name"?}, which replaces the todo's) into
// `patch`; null clears them. Returns an error message, or "" if they're fine.
std::string parseExtras(const Json::Value &body, TodoPatch &patch)
{
    if (body.isMember("priority"))
    {
        const auto &v = body["priority"];
        const int p = v.isNull() ? 0 : v.isString() ? parsePriority(v.asString()) : -1;
        if (p < 0)
            return "priority must be \"low\", \"medium\", \"high\" or null";
        patch.priority = p;
    }
    if (body.isMember("deadline"))
    {
        const auto &v = body["deadline"];
        if (!v.isNull() && !(v.isString() && isValidDate(v.asString())))
            return "deadline must be a date like \"2026-10-31\", or null";
        patch.deadline = v.isNull() ? "" : v.asString();
    }
    if (body.isMember("description"))
    {
        const auto &v = body["description"];
        if (!v.isNull() && !v.isString())
            return "description must be a string or null";
        // Trailing blank lines and spaces don't count; all-blank clears it.
        std::string d = v.isNull() ? "" : v.asString();
        d.erase(d.find_last_not_of(" \t\r\n") + 1);
        if (d.size() > kMaxDescription)
            return "description must be at most 10000 characters";
        patch.description = d;
    }
    if (!http::parseRef(body, "roomId", patch.roomId))
        return "roomId must be an id or null";
    if (body.isMember("attachments"))
    {
        std::vector<Attachment> list;
        if (auto msg = files::parseList(body["attachments"], list); !msg.empty())
            return msg;
        patch.attachments = std::move(list);
    }
    return "";
}

// The agent making a change (X-Todo-Agent or ?agent=), for room announcements.
std::string actor(const HttpRequestPtr &req)
{
    std::string err;
    return http::agent(req, nullptr, "agent", err);
}
}  // namespace

// GET /api/todos[?status=all|pending|done][&archived=1][&room=ID]
void TodoController::list(const HttpRequestPtr &req, Callback &&cb)
{
    const auto status = req->getParameter("status");
    const auto room = req->getParameter("room");
    const int64_t roomId = std::atoll(room.c_str());
    Json::Value arr(Json::arrayValue);
    for (const auto &t : TodoStore::get().list(http::wantsArchived(req)))
    {
        if ((status == "pending" && t.done) || (status == "done" && !t.done))
            continue;
        if (!room.empty() && t.roomId != roomId)
            continue;
        arr.append(t.toJson());
    }
    cb(json(arr));
}

// POST /api/todos  {"title": "...", "categoryId"?: id|null, "roomId"?: id|null,
//                   "priority"?, "deadline"?, "description"?, "attachments"?}
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
    TodoPatch extras;
    if (auto msg = parseExtras(*body, extras); !msg.empty())
        return cb(error(400, msg));

    Todo init;
    init.categoryId = categoryId.value_or(0);
    init.priority = extras.priority.value_or(0);
    init.deadline = extras.deadline.value_or("");
    init.description = extras.description.value_or("");
    init.roomId = extras.roomId.value_or(0);
    init.attachments = extras.attachments.value_or(std::vector<Attachment>{});
    try
    {
        cb(json(TodoStore::get().add(title, init, actor(req)).toJson(), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// PATCH /api/todos/{id}  {"title"?: "...", "done"?: bool, "categoryId"?: id|null,
//                         "priority"?: "low"|"medium"|"high"|null, "deadline"?: "YYYY-MM-DD"|null,
//                         "description"?: "..."|null, "roomId"?: id|null,
//                         "attachments"?: [{"id", "name"?}...]|null}
void TodoController::update(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return cb(error(400, "body must be a JSON object"));

    TodoPatch patch;
    if (body->isMember("title"))
    {
        if (!(*body)["title"].isString())
            return cb(error(400, "title must be a string"));
        patch.title = http::trim((*body)["title"].asString());
        if (patch.title->empty() || patch.title->size() > kMaxTitle)
            return cb(error(400, "title must be 1-500 characters"));
    }
    if (body->isMember("done"))
    {
        if (!(*body)["done"].isBool())
            return cb(error(400, "done must be a boolean"));
        patch.done = (*body)["done"].asBool();
    }
    if (!http::parseRef(*body, "categoryId", patch.categoryId))
        return cb(error(400, "categoryId must be an id or null"));
    if (auto msg = parseExtras(*body, patch); !msg.empty())
        return cb(error(400, msg));
    if (patch.empty())
        return cb(error(400, "nothing to update"));

    try
    {
        auto t = TodoStore::get().update(id, patch, actor(req));
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
void TodoController::remove(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    if (!TodoStore::get().remove(id, actor(req)))
        return cb(error(404, "todo not found"));
    cb(HttpResponse::newHttpResponse(k204NoContent, CT_NONE));
}

// DELETE /api/todos/done  -- remove completed todos, except archived ones
void TodoController::clearDone(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value body;
    body["removed"] = static_cast<Json::UInt64>(TodoStore::get().clearDone());
    cb(json(body));
}

// ── Claims ──

namespace
{
// POST /api/todos/{id}/claim|release  {"agent"?: "...", "force"?: bool}
void claimOrRelease(const HttpRequestPtr &req, TodoController::Callback &cb, int64_t id, bool claim)
{
    auto body = req->getJsonObject();
    std::string err;
    const auto agent = http::agent(req, body.get(), "agent", err);
    if (!err.empty())
        return cb(error(400, err));
    if (agent.empty())
        return cb(error(400, "say who you are: \"agent\", or the X-Todo-Agent header"));
    const bool force = body && (*body)["force"].asBool();
    try
    {
        auto &store = TodoStore::get();
        cb(json((claim ? store.claim(id, agent, force) : store.release(id, agent, force)).toJson()));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}
}  // namespace

// POST /api/todos/{id}/claim  -- 409 if someone else has it, unless "force"
void TodoController::claim(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    claimOrRelease(req, cb, id, true);
}

// POST /api/todos/{id}/release  -- someone else's claim needs "force"
void TodoController::release(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    claimOrRelease(req, cb, id, false);
}

// ── Pruned ──

// POST /api/todos/{id}/prune  -- a done todo; → the pruned todo (its new pN id)
void TodoController::prune(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    try
    {
        cb(json(TodoStore::get().prune(id).toJson()));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// POST /api/todos/prune  -- every done todo, except in archived categories
void TodoController::pruneDone(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value body;
    body["pruned"] = static_cast<Json::UInt64>(TodoStore::get().pruneDone());
    cb(json(body));
}

// GET /api/pruned
void TodoController::listPruned(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &t : TodoStore::get().listPruned())
        arr.append(t.toJson());
    cb(json(arr));
}

// DELETE /api/pruned  -- deletes every pruned todo for good
void TodoController::clearPruned(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value body;
    body["removed"] = static_cast<Json::UInt64>(TodoStore::get().clearPruned());
    cb(json(body));
}

// POST /api/pruned/{id}/restore  -- → the todo, done, with a free id
void TodoController::restore(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    try
    {
        cb(json(TodoStore::get().restore(id).toJson(), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// DELETE /api/pruned/{id}
void TodoController::removePruned(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    if (!TodoStore::get().removePruned(id))
        return cb(error(404, "pruned todo not found"));
    cb(HttpResponse::newHttpResponse(k204NoContent, CT_NONE));
}
