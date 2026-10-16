#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class RoomController : public HttpController<RoomController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RoomController::list, "/api/rooms", Get);
    ADD_METHOD_TO(RoomController::create, "/api/rooms", Post);
    ADD_METHOD_TO(RoomController::show, "/api/rooms/{id}", Get);
    ADD_METHOD_TO(RoomController::update, "/api/rooms/{id}", Patch);
    ADD_METHOD_TO(RoomController::remove, "/api/rooms/{id}", Delete);
    ADD_METHOD_TO(RoomController::removeMember, "/api/rooms/{id}/members", Delete);
    ADD_METHOD_TO(RoomController::messages, "/api/rooms/{id}/messages", Get);
    ADD_METHOD_TO(RoomController::post, "/api/rooms/{id}/messages", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void list(const HttpRequestPtr &req, Callback &&cb);
    void create(const HttpRequestPtr &req, Callback &&cb);
    void show(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void update(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void remove(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void removeMember(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void messages(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void post(const HttpRequestPtr &req, Callback &&cb, int64_t id);
};
