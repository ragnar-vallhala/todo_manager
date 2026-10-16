#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class TodoController : public HttpController<TodoController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TodoController::list, "/api/todos", Get);
    ADD_METHOD_TO(TodoController::create, "/api/todos", Post);
    ADD_METHOD_TO(TodoController::clearDone, "/api/todos/done", Delete);
    ADD_METHOD_TO(TodoController::update, "/api/todos/{id}", Patch);
    ADD_METHOD_TO(TodoController::remove, "/api/todos/{id}", Delete);
    ADD_METHOD_TO(TodoController::pruneDone, "/api/todos/prune", Post);
    ADD_METHOD_TO(TodoController::prune, "/api/todos/{id}/prune", Post);
    ADD_METHOD_TO(TodoController::claim, "/api/todos/{id}/claim", Post);
    ADD_METHOD_TO(TodoController::release, "/api/todos/{id}/release", Post);
    ADD_METHOD_TO(TodoController::listPruned, "/api/pruned", Get);
    ADD_METHOD_TO(TodoController::clearPruned, "/api/pruned", Delete);
    ADD_METHOD_TO(TodoController::restore, "/api/pruned/{id}/restore", Post);
    ADD_METHOD_TO(TodoController::removePruned, "/api/pruned/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void list(const HttpRequestPtr &req, Callback &&cb);
    void create(const HttpRequestPtr &req, Callback &&cb);
    void update(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void remove(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void clearDone(const HttpRequestPtr &req, Callback &&cb);
    void prune(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void claim(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void release(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void pruneDone(const HttpRequestPtr &req, Callback &&cb);
    void listPruned(const HttpRequestPtr &req, Callback &&cb);
    void clearPruned(const HttpRequestPtr &req, Callback &&cb);
    void restore(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void removePruned(const HttpRequestPtr &req, Callback &&cb, int64_t id);
};
