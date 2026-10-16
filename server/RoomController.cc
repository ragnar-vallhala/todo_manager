#include "RoomController.h"
#include "HttpUtil.h"
#include "RoomHub.h"
#include "Rooms.h"

#include <algorithm>
#include <cstdlib>

using http::error;
using http::json;

// GET /api/rooms
void RoomController::list(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &r : TodoStore::get().listRooms())
        arr.append(rooms::roomJson(r));
    cb(json(arr));
}

// POST /api/rooms  {"name": "...", "topic"?: "..."}
void RoomController::create(const HttpRequestPtr &req, Callback &&cb)
{
    auto body = req->getJsonObject();
    if (!body || !(*body)["name"].isString())
        return cb(error(400, "body must be JSON with a \"name\" string"));
    const auto name = http::trim((*body)["name"].asString());
    if (auto msg = rooms::checkName(name); !msg.empty())
        return cb(error(400, msg));
    const auto &t = (*body)["topic"];
    if (!t.isNull() && !t.isString())
        return cb(error(400, "topic must be a string or null"));
    const auto topic = http::trim(t.asString());
    if (topic.size() > rooms::kMaxTopic)
        return cb(error(400, "topic must be at most 500 characters"));
    try
    {
        cb(json(rooms::roomJson(TodoStore::get().addRoom(name, topic)), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// GET /api/rooms/{id}
void RoomController::show(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    try
    {
        cb(json(rooms::roomJson(TodoStore::get().room(id))));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// PATCH /api/rooms/{id}  {"name"?: "...", "topic"?: "..."|null}
void RoomController::update(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return cb(error(400, "body must be a JSON object"));
    std::optional<std::string> name, topic;
    if (body->isMember("name"))
    {
        if (!(*body)["name"].isString())
            return cb(error(400, "name must be a string"));
        name = http::trim((*body)["name"].asString());
        if (auto msg = rooms::checkName(*name); !msg.empty())
            return cb(error(400, msg));
    }
    if (body->isMember("topic"))
    {
        const auto &t = (*body)["topic"];
        if (!t.isNull() && !t.isString())
            return cb(error(400, "topic must be a string or null"));
        topic = http::trim(t.asString());
        if (topic->size() > rooms::kMaxTopic)
            return cb(error(400, "topic must be at most 500 characters"));
    }
    if (!name && !topic)
        return cb(error(400, "nothing to update"));
    try
    {
        cb(json(rooms::roomJson(TodoStore::get().updateRoom(id, name, topic))));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// DELETE /api/rooms/{id}  -- deletes its messages; its todos stay
void RoomController::remove(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    try
    {
        TodoStore::get().removeRoom(id);
        cb(HttpResponse::newHttpResponse(k204NoContent, CT_NONE));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// DELETE /api/rooms/{id}/members?name=NAME  -- forgets a member, e.g. an old
// agent, so it stops being suggested; anyone active rejoins on their next visit
void RoomController::removeMember(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    const auto name = req->getParameter("name");
    if (name.empty())
        return cb(error(400, "say which member: ?name=NAME"));
    try
    {
        cb(json(rooms::roomJson(TodoStore::get().removeMember(id, name))));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// GET /api/rooms/{id}/messages[?since=N][&limit=N][&wait=SECONDS][&agent=NAME]
// With `wait`, answers as soon as there's a message after `since`, or with
// [] once `wait` seconds pass.
void RoomController::messages(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    std::string err;
    const auto agent = http::agent(req, nullptr, "agent", err);
    if (!err.empty())
        return cb(error(400, err));
    try
    {
        TodoStore::get().room(id);
    }
    catch (const StoreError &e)
    {
        return cb(error(e));
    }
    TodoStore::get().touch(id, agent);

    const int64_t since = std::atoll(req->getParameter("since").c_str());
    const auto limitParam = req->getParameter("limit");
    const size_t limit =
        limitParam.empty() ? rooms::kDefaultLimit
                           : std::clamp<long long>(std::atoll(limitParam.c_str()), 1, rooms::kMaxLimit);
    const double wait = std::clamp(std::atof(req->getParameter("wait").c_str()), 0.0, rooms::kMaxWait);

    RoomHub::get().wait(id, since, limit, wait, [cb = std::move(cb)](std::vector<Message> msgs) {
        Json::Value arr(Json::arrayValue);
        for (const auto &m : msgs)
            arr.append(m.toJson());
        cb(json(arr));
    });
}

// POST /api/rooms/{id}/messages  {"text": "...", "author"?: "..."}
// The author can also come from the X-Todo-Agent header.
void RoomController::post(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    auto body = req->getJsonObject();
    if (!body || !(*body)["text"].isString())
        return cb(error(400, "body must be JSON with a \"text\" string"));
    std::string text = (*body)["text"].asString();
    text.erase(text.find_last_not_of(" \t\r\n") + 1);
    text.erase(0, text.find_first_not_of(" \t\r\n"));
    if (text.empty() || text.size() > rooms::kMaxMessage)
        return cb(error(400, "text must be 1-10000 characters"));
    std::string err;
    const auto author = http::agent(req, body.get(), "author", err);
    if (!err.empty())
        return cb(error(400, err));
    if (author.empty())
        return cb(error(400, "say who you are: \"author\", or the X-Todo-Agent header"));
    try
    {
        cb(json(TodoStore::get().post(id, author, text).toJson(), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}
