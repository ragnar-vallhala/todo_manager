#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// Claude usage reports, one per device, kept in memory only.
class UsageController : public HttpController<UsageController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UsageController::list, "/api/usage", Get);
    ADD_METHOD_TO(UsageController::report, "/api/usage", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void list(const HttpRequestPtr &req, Callback &&cb);
    void report(const HttpRequestPtr &req, Callback &&cb);
};
