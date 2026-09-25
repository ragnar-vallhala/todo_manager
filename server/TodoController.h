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
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void list(const HttpRequestPtr &req, Callback &&cb);
    void create(const HttpRequestPtr &req, Callback &&cb);
    void update(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void remove(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void clearDone(const HttpRequestPtr &req, Callback &&cb);
};
