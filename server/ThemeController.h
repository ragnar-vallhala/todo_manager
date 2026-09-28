#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class ThemeController : public HttpController<ThemeController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ThemeController::list, "/api/themes", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void list(const HttpRequestPtr &req, Callback &&cb);
};
