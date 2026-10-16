#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// An MCP server (Streamable HTTP transport, JSON responses) so AI agents can
// use rooms as tools: `claude mcp add --transport http todo http://HOST:27183/mcp`.
class McpController : public HttpController<McpController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(McpController::handle, "/mcp", Post);
    ADD_METHOD_TO(McpController::noStream, "/mcp", Get, Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void handle(const HttpRequestPtr &req, Callback &&cb);
    void noStream(const HttpRequestPtr &req, Callback &&cb);
};
