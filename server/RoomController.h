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
    ADD_METHOD_TO(RoomController::files, "/api/rooms/{id}/files", Get);
    ADD_METHOD_TO(RoomController::pruneAll, "/api/rooms/{id}/messages/prune", Post);
    ADD_METHOD_TO(RoomController::prune, "/api/rooms/{id}/messages/{mid}/prune", Post);
    ADD_METHOD_TO(RoomController::listPruned, "/api/rooms/{id}/pruned", Get);
    ADD_METHOD_TO(RoomController::clearPruned, "/api/rooms/{id}/pruned", Delete);
    ADD_METHOD_TO(RoomController::restore, "/api/rooms/{id}/pruned/{pid}/restore", Post);
    ADD_METHOD_TO(RoomController::removePruned, "/api/rooms/{id}/pruned/{pid}", Delete);
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
    void files(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void pruneAll(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void prune(const HttpRequestPtr &req, Callback &&cb, int64_t id, int64_t mid);
    void listPruned(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void clearPruned(const HttpRequestPtr &req, Callback &&cb, int64_t id);
    void restore(const HttpRequestPtr &req, Callback &&cb, int64_t id, int64_t pid);
    void removePruned(const HttpRequestPtr &req, Callback &&cb, int64_t id, int64_t pid);
};
