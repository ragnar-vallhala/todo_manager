#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class CategoryController : public HttpController<CategoryController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CategoryController::list, "/api/categories", Get);
    ADD_METHOD_TO(CategoryController::create, "/api/categories", Post);
    ADD_METHOD_TO(CategoryController::update, "/api/categories/{id}", Patch);
    ADD_METHOD_TO(CategoryController::remove, "/api/categories/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void list(const HttpRequestPtr &req, Callback &&cb);
    void create(const HttpRequestPtr &req, Callback &&cb);
    void update(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void remove(const HttpRequestPtr &req, Callback &&cb, int64_t id);
};
