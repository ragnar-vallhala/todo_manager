// Thin synchronous client for the todo server's REST API.
#pragma once

#include "Categories.h"
#include "TodoItem.h"

#include <drogon/HttpClient.h>
#include <trantor/net/EventLoopThread.h>

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
    explicit Api(std::string server, double timeoutSec = 5.0)
        : server_(std::move(server)), timeout_(timeoutSec)
    {
        loopThread_.run();
        client_ = drogon::HttpClient::newHttpClient(server_, loopThread_.getLoop());
    }
    ~Api() { client_.reset(); }  // release client before its loop stops

    const std::string &server() const { return server_; }

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

    // `priority` 0, `deadline` "" and `description` "" mean none.
    TodoItem add(const std::string &title, int64_t categoryId = 0, int priority = 0,
                 const std::string &deadline = "", const std::string &description = "")
    {
        Json::Value body;
        body["title"] = title;
        body["categoryId"] = ref(categoryId);
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

    // Sets whichever is given; priority 0 / deadline "" clear them.
    // "" clears it.
    TodoItem setDescription(int64_t id, const std::string &description)
    {
        Json::Value body;
        body["description"] = description.empty() ? Json::Value() : Json::Value(description);
        return TodoItem::fromJson(send(drogon::Patch, path(id), &body));
    }

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

  private:
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
    Json::Value send(drogon::HttpMethod method,
                     const std::string &path,
                     const Json::Value *body = nullptr)
    {
        using namespace drogon;
        auto req = body ? HttpRequest::newHttpJsonRequest(*body)
                        : HttpRequest::newHttpRequest();
        req->setMethod(method);
        req->setPath(path);

        auto [result, resp] = client_->sendRequest(req, timeout_);
        if (result != ReqResult::Ok || !resp)
            throw ApiError("cannot reach server at " + server_ + " (" +
                               to_string(result) + ")",
                           true);

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
    trantor::EventLoopThread loopThread_;
    drogon::HttpClientPtr client_;
};
