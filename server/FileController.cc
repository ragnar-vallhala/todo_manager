#include "FileController.h"
#include "Files.h"
#include "HttpUtil.h"

#include <drogon/MultiPart.h>
#include <drogon/utils/Utilities.h>

#include <fstream>

using http::error;
using http::json;

// POST /api/files  -- the file as multipart/form-data (field "file"), or as
// the raw body with ?name=NAME (or an X-Filename header). PNG, JPEG, GIF,
// WebP or PDF, at most 10 MB. 201 → {"id", "name", "type", "size",
// "width"?, "height"?}; the same bytes always get the same id.
void FileController::upload(const HttpRequestPtr &req, Callback &&cb)
{
    std::string_view data;
    std::string name;
    MultiPartParser parts;
    if (req->contentType() == CT_MULTIPART_FORM_DATA)
    {
        if (parts.parse(req) != 0 || parts.getFiles().empty())
            return cb(error(400, "send the file as a multipart field named \"file\""));
        const HttpFile *file = &parts.getFiles().front();
        for (const auto &f : parts.getFiles())
            if (f.getItemName() == "file")
                file = &f;
        data = file->fileContent();
        name = file->getFileName();
    }
    else
    {
        data = req->body();
        name = req->getParameter("name");
        if (name.empty())
            name = req->getHeader("x-filename");
    }
    try
    {
        cb(json(files::store(data, name).toJson(), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// GET /api/files/{id}[?name=NAME][&download=1]  -- the file, with its type.
// `name` is what browsers call it when saving; `download` saves instead of
// showing it.
void FileController::download(const HttpRequestPtr &req, Callback &&cb, const std::string &id)
{
    const std::string path = files::pathOf(id);
    std::ifstream in(path, std::ios::binary);
    if (path.empty() || !in)
        return cb(error(404, "file not found"));
    char head[16] = {};
    in.read(head, sizeof(head));
    const std::string type = files::sniff(std::string_view(head, static_cast<size_t>(in.gcount())));

    auto resp = HttpResponse::newFileResponse(path, "", CT_CUSTOM, type, req);
    const auto name = req->getParameter("name");
    const bool save = req->getParameter("download") == "1";
    if (!name.empty() || save)
    {
        // RFC 5987 wants %20 for spaces, not "+" (a real "+" comes out as %2B).
        std::string encoded = drogon::utils::urlEncodeComponent(files::cleanName(name, type));
        for (size_t p; (p = encoded.find('+')) != std::string::npos;)
            encoded.replace(p, 1, "%20");
        resp->addHeader("Content-Disposition",
                        std::string(save ? "attachment" : "inline") + "; filename*=UTF-8''" + encoded);
    }
    // The id is the content's hash, so it never changes.
    resp->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    resp->addHeader("X-Content-Type-Options", "nosniff");
    cb(resp);
}
