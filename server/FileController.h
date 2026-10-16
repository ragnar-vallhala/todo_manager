#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// Uploading and downloading attachments (see Files.h).
class FileController : public HttpController<FileController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FileController::upload, "/api/files", Post);
    ADD_METHOD_TO(FileController::download, "/api/files/{id}", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr &)>;

    void upload(const HttpRequestPtr &req, Callback &&cb);
    void download(const HttpRequestPtr &req, Callback &&cb, const std::string &id);
};
