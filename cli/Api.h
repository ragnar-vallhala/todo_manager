// Thin synchronous client for the todo server's REST API.
#pragma once

#include "Categories.h"
#include "Rooms.h"
#include "TodoItem.h"

#include <drogon/HttpClient.h>
#include <trantor/net/EventLoopThread.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct ApiError : std::runtime_error
{
    ApiError(const std::string &msg, bool offline)
        : std::runtime_error(msg), offline(offline)
    {
    }
    bool offline;  // true if the server could not be reached at all
};

class Api
{
  public:
    // `agent` is who we are in rooms, sent with every request (X-Todo-Agent).
    explicit Api(std::string server, double timeoutSec = 5.0, std::string agent = "")
        : server_(std::move(server)), timeout_(timeoutSec), agent_(std::move(agent))
    {
        loopThread_.run();
        client_ = drogon::HttpClient::newHttpClient(server_, loopThread_.getLoop());
    }
    ~Api() { client_.reset(); }  // release client before its loop stops

    const std::string &server() const { return server_; }
    const std::string &agent() const { return agent_; }

    // With `archived`, also todos in archived categories.
    std::vector<TodoItem> list(const std::string &status = "all", bool archived = false)
    {
        std::vector<TodoItem> out;
        auto json = send(drogon::Get, "/api/todos?status=" + status +
                                          (archived ? "&archived=1" : ""));
        for (const auto &v : json)
            out.push_back(TodoItem::fromJson(v));
        return out;
    }

    // `priority` 0, `deadline` "", `description` "" and `roomId` 0 mean none.
    TodoItem add(const std::string &title, int64_t categoryId = 0, int priority = 0,
                 const std::string &deadline = "", const std::string &description = "",
                 int64_t roomId = 0)
    {
        Json::Value body;
        body["title"] = title;
        body["categoryId"] = ref(categoryId);
        if (roomId)
            body["roomId"] = ref(roomId);
        if (priority)
            body["priority"] = todo::priorityName(priority);
        if (!deadline.empty())
            body["deadline"] = deadline;
        if (!description.empty())
            body["description"] = description;
        return TodoItem::fromJson(send(drogon::Post, "/api/todos", &body));
    }

    TodoItem setDone(int64_t id, bool done)
    {
        Json::Value body;
        body["done"] = done;
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    TodoItem rename(int64_t id, const std::string &title)
    {
        Json::Value body;
        body["title"] = title;
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    TodoItem move(int64_t id, int64_t categoryId)
    {
        Json::Value body;
        body["categoryId"] = ref(categoryId);
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    // "" clears it.
    TodoItem setDescription(int64_t id, const std::string &description)
    {
        Json::Value body;
        body["description"] = description.empty() ? Json::Value() : Json::Value(description);
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    // Sets whichever is given; priority 0 / deadline "" clear them.
    TodoItem setExtras(int64_t id, std::optional<int> priority,
                       const std::optional<std::string> &deadline)
    {
        Json::Value body;
        if (priority)
            body["priority"] = *priority ? Json::Value(todo::priorityName(*priority)) : Json::Value();
        if (deadline)
            body["deadline"] = deadline->empty() ? Json::Value() : Json::Value(*deadline);
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    // 0 takes it out of its room.
    TodoItem setRoom(int64_t id, int64_t roomId)
    {
        Json::Value body;
        body["roomId"] = ref(roomId);
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    // Claims are how agents split up work. Claiming one someone else holds,
    // or releasing their claim, fails unless `force`.
    TodoItem claim(int64_t id, bool force = false)
    {
        Json::Value body;
        body["force"] = force;
        return TodoItem::fromJson(send(drogon::Post, path(id) + "/claim", &body));
    }

    TodoItem release(int64_t id, bool force = false)
    {
        Json::Value body;
        body["force"] = force;
        return TodoItem::fromJson(send(drogon::Post, path(id) + "/release", &body));
    }

    void remove(int64_t id) { send(drogon::Delete, path(id)); }

    // Pruning moves done todos to the pruned list (ids p1, p2...), freeing
    // their ids for new todos.
    TodoItem prune(int64_t id)
    {
        return TodoItem::fromJson(send(drogon::Post, path(id) + "/prune"));
    }

    uint64_t pruneDone() { return send(drogon::Post, "/api/todos/prune")["pruned"].asUInt64(); }

    std::vector<TodoItem> listPruned()
    {
        std::vector<TodoItem> out;
        for (const auto &v : send(drogon::Get, "/api/pruned"))
            out.push_back(TodoItem::fromJson(v));
        return out;
    }

    // Back as a done todo, with a free id.
    TodoItem restore(int64_t prunedId)
    {
        return TodoItem::fromJson(
            send(drogon::Post, "/api/pruned/" + std::to_string(prunedId) + "/restore"));
    }

    void removePruned(int64_t prunedId)
    {
        send(drogon::Delete, "/api/pruned/" + std::to_string(prunedId));
    }

    uint64_t clearPruned() { return send(drogon::Delete, "/api/pruned")["removed"].asUInt64(); }

    // With `archived`, archived categories are included (see CategoryTree::inArchive).
    CategoryTree categories(bool archived = false)
    {
        std::vector<CategoryItem> out;
        for (const auto &v : send(drogon::Get, archived ? "/api/categories?archived=1"
                                                        : "/api/categories"))
            out.push_back(CategoryItem::fromJson(v));
        return CategoryTree(std::move(out));
    }

    CategoryItem addCategory(const std::string &name, int64_t parentId)
    {
        Json::Value body;
        body["name"] = name;
        body["parentId"] = ref(parentId);
        return CategoryItem::fromJson(send(drogon::Post, "/api/categories", &body));
    }

    CategoryItem renameCategory(int64_t id, const std::string &name)
    {
        Json::Value body;
        body["name"] = name;
        return CategoryItem::fromJson(send(drogon::Patch, catPath(id), &body));
    }

    CategoryItem moveCategory(int64_t id, int64_t parentId)
    {
        Json::Value body;
        body["parentId"] = ref(parentId);
        return CategoryItem::fromJson(send(drogon::Patch, catPath(id), &body));
    }

    CategoryItem setCategoryArchived(int64_t id, bool archived)
    {
        Json::Value body;
        body["archived"] = archived;
        return CategoryItem::fromJson(send(drogon::Patch, catPath(id), &body));
    }

    // An empty style clears it, so the category looks like its parent.
    CategoryItem setCategoryStyle(int64_t id, const CategoryStyle &style)
    {
        Json::Value body;
        body["style"] = style.toJson();
        return CategoryItem::fromJson(send(drogon::Patch, catPath(id), &body));
    }

    void removeCategory(int64_t id) { send(drogon::Delete, catPath(id)); }

    // ── rooms ──

    // Claude usage reports from all devices (see Usage.h).
    Json::Value usage() { return send(drogon::Get, "/api/usage"); }

    // With `archived`, archived rooms are included.
    std::vector<RoomItem> rooms(bool archived = false)
    {
        std::vector<RoomItem> out;
        for (const auto &v : send(drogon::Get, archived ? "/api/rooms?archived=1" : "/api/rooms"))
            out.push_back(RoomItem::fromJson(v));
        return out;
    }

    RoomItem addRoom(const std::string &name, const std::string &topic)
    {
        Json::Value body;
        body["name"] = name;
        if (!topic.empty())
            body["topic"] = topic;
        return RoomItem::fromJson(send(drogon::Post, "/api/rooms", &body));
    }

    // Changes whichever is given; a topic of "" clears it.
    RoomItem updateRoom(int64_t id, const std::optional<std::string> &name,
                        const std::optional<std::string> &topic)
    {
        Json::Value body;
        if (name)
            body["name"] = *name;
        if (topic)
            body["topic"] = topic->empty() ? Json::Value() : Json::Value(*topic);
        return RoomItem::fromJson(send(drogon::Patch, roomPath(id), &body));
    }

    // Archiving moves the log to the pruned messages (freeing their ids);
    // unarchiving puts them back.
    RoomItem setRoomArchived(int64_t id, bool archived)
    {
        Json::Value body;
        body["archived"] = archived;
        return RoomItem::fromJson(send(drogon::Patch, roomPath(id), &body));
    }

    // Deletes its messages too; its todos stay, outside any room.
    void removeRoom(int64_t id) { send(drogon::Delete, roomPath(id)); }

    // Drops `name` from the room's members; they rejoin if they come back.
    RoomItem removeMember(int64_t id, const std::string &name)
    {
        return RoomItem::fromJson(
            send(drogon::Delete, roomPath(id) + "/members", nullptr, 0, {{"name", name}}));
    }

    // Messages after the cursor `since` (a seq; the newest `limit` of them). With `wait`
    // seconds, the server holds the request until there is one, or answers
    // with none when the time is up.
    std::vector<MessageItem> messages(int64_t roomId, int64_t since, size_t limit = 100,
                                      double wait = 0)
    {
        std::string q = roomPath(roomId) + "/messages?since=" + std::to_string(since) +
                        "&limit=" + std::to_string(limit);
        if (wait > 0)
            q += "&wait=" + std::to_string(static_cast<int>(wait));
        std::vector<MessageItem> out;
        for (const auto &v : send(drogon::Get, q, nullptr, wait > 0 ? wait + 15 : 0))
            out.push_back(MessageItem::fromJson(v));
        return out;
    }

    // A room's attachments, newest first, each with the message it's on;
    // from messages before seq `before` (0: from the newest).
    std::vector<RoomFileItem> roomFiles(int64_t roomId, int64_t before = 0, size_t limit = 1000)
    {
        std::string q = roomPath(roomId) + "/files?limit=" + std::to_string(limit);
        if (before > 0)
            q += "&before=" + std::to_string(before);
        std::vector<RoomFileItem> out;
        for (const auto &v : send(drogon::Get, q))
            out.push_back(RoomFileItem::fromJson(v));
        return out;
    }

    // `text` may be "" when there are attachments (uploaded with upload()).
    MessageItem post(int64_t roomId, const std::string &text,
                     const std::vector<AttachmentItem> &attachments = {})
    {
        Json::Value body;
        body["text"] = text;
        if (!attachments.empty())
            body["attachments"] = AttachmentItem::listToJson(attachments);
        return MessageItem::fromJson(send(drogon::Post, roomPath(roomId) + "/messages", &body));
    }

    // ── files ──

    // Uploads the file at `path` (an image or PDF up to 10 MB, a video up to 50 MB) so it can
    // be attached; throws ApiError if it can't be read or the server says no.
    AttachmentItem upload(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw ApiError("can't read " + path, false);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto type = attach::sniff(path);
        if (static_cast<int64_t>(data.size()) > attach::maxFor(type))
            throw ApiError(path + " is over " + attach::maxLabel(type) + ", the most the server takes", false);
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath("/api/files");
        req->setParameter("name", attach::baseName(path));
        req->setContentTypeString("application/octet-stream");
        req->setBody(std::move(data));
        return AttachmentItem::fromJson(sendRequest(req, 300));
    }

    // The bytes of file `id`.
    std::string download(const std::string &id)
    {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath("/api/files/" + id);
        std::string body;
        sendRequest(req, 300, &body);
        return body;
    }

    // Replaces the todo's attachments.
    TodoItem setAttachments(int64_t id, const std::vector<AttachmentItem> &attachments)
    {
        Json::Value body;
        body["attachments"] = AttachmentItem::listToJson(attachments);
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

    // Pruning moves messages out of the log (they get pN ids), freeing their ids.
    MessageItem pruneMessage(int64_t roomId, int64_t id)
    {
        return MessageItem::fromJson(
            send(drogon::Post, roomPath(roomId) + "/messages/" + std::to_string(id) + "/prune"));
    }

    // All but the newest `keep`; returns how many.
    size_t pruneMessages(int64_t roomId, size_t keep)
    {
        Json::Value body;
        body["keep"] = static_cast<Json::UInt64>(keep);
        return send(drogon::Post, roomPath(roomId) + "/messages/prune", &body)["pruned"].asUInt64();
    }

    std::vector<MessageItem> prunedMessages(int64_t roomId)
    {
        std::vector<MessageItem> out;
        for (const auto &v : send(drogon::Get, roomPath(roomId) + "/pruned"))
            out.push_back(MessageItem::fromJson(v));
        return out;
    }

    MessageItem restoreMessage(int64_t roomId, int64_t prunedId)
    {
        return MessageItem::fromJson(send(
            drogon::Post, roomPath(roomId) + "/pruned/" + std::to_string(prunedId) + "/restore"));
    }

    void removePrunedMessage(int64_t roomId, int64_t prunedId)
    {
        send(drogon::Delete, roomPath(roomId) + "/pruned/" + std::to_string(prunedId));
    }

    size_t clearPrunedMessages(int64_t roomId)
    {
        return send(drogon::Delete, roomPath(roomId) + "/pruned")["removed"].asUInt64();
    }

  private:
    static std::string roomPath(int64_t id) { return "/api/rooms/" + std::to_string(id); }

    static Json::Value ref(int64_t id)
    {
        return id ? Json::Value(static_cast<Json::Int64>(id)) : Json::Value();
    }

    static std::string catPath(int64_t id)
    {
        return "/api/categories/" + std::to_string(id);
    }

    static std::string path(int64_t id)
    {
        return "/api/todos/" + std::to_string(id);
    }

    // Returns the JSON body (null for empty responses); throws ApiError.
    // `timeout` 0 uses the usual one; `params` go in the query string, encoded.
    Json::Value send(drogon::HttpMethod method,
                     const std::string &path,
                     const Json::Value *body = nullptr,
                     double timeout = 0,
                     const std::vector<std::pair<std::string, std::string>> &params = {})
    {
        using namespace drogon;
        auto req = body ? HttpRequest::newHttpJsonRequest(*body)
                        : HttpRequest::newHttpRequest();
        req->setMethod(method);
        req->setPath(path);
        for (const auto &[k, v] : params)
            req->setParameter(k, v);
        return sendRequest(req, timeout);
    }

    // Sends `req` (as us); the JSON body back, or with `raw`, the body
    // itself goes there instead.
    Json::Value sendRequest(const drogon::HttpRequestPtr &req, double timeout,
                            std::string *raw = nullptr)
    {
        using namespace drogon;
        if (!agent_.empty())
            req->addHeader("X-Todo-Agent", agent_);

        auto [result, resp] = client_->sendRequest(req, timeout > 0 ? timeout : timeout_);
        if (result != ReqResult::Ok || !resp)
            throw ApiError("cannot reach server at " + server_ + " (" +
                               to_string(result) + ")",
                           true);

        if (raw && resp->statusCode() < 400)
        {
            *raw = std::string(resp->body());
            return Json::Value();
        }
        auto json = resp->getJsonObject();
        if (resp->statusCode() >= 400)
            throw ApiError(json && json->isMember("error")
                               ? (*json)["error"].asString()
                               : "HTTP " + std::to_string(resp->statusCode()),
                           false);
        return json ? *json : Json::Value();
    }

    std::string server_;
    double timeout_;
    std::string agent_;
    trantor::EventLoopThread loopThread_;
    drogon::HttpClientPtr client_;
};
