#include "FileController.h"
#include "Files.h"
#include "HttpUtil.h"

#include <drogon/MultiPart.h>
#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <fstream>
#include <string>

using http::error;
using http::json;

// POST /api/files  -- the file as multipart/form-data (field "file"), or as
// the raw body with ?name=NAME (or an X-Filename header). PNG, JPEG, GIF,
// WebP up to 10 MB, MP4 or WebM video up to 50 MB, or any other file (a
// "document") up to 200 MB. 201 → {"id", "name", "type", "kind", "size",
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

// GET /api/files/{id}[?name=NAME][&download=1]  -- the file, with its type;
// a Range header gets that part of it (206).
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
    std::string type = files::sniff(std::string_view(head, static_cast<size_t>(in.gcount())));
    const bool document = files::kindOf(type) == "document";
    if (type.empty())
        type = files::typeForName(req->getParameter("name"));

    // One byte range ("bytes=A-B", "bytes=A-" or "bytes=-N"), so players can
    // seek and stream; anything else gets the whole file.
    in.clear();
    const auto size = static_cast<size_t>(in.seekg(0, std::ios::end).tellg());
    size_t offset = 0, length = 0;
    bool partial = false;
    if (const auto &range = req->getHeader("range"); range.rfind("bytes=", 0) == 0 && range.find(',') == std::string::npos)
    {
        const std::string spec = range.substr(6);
        const auto dash = spec.find('-');
        const std::string a = spec.substr(0, dash), b = dash == std::string::npos ? "" : spec.substr(dash + 1);
        auto digits = [](const std::string &s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; };
        if (dash != std::string::npos && (digits(a) || digits(b)) && (a.empty() || digits(a)) && (b.empty() || digits(b)))
        {
            unsigned long long first, last;
            if (a.empty())
            {
                const auto n = std::min<unsigned long long>(std::stoull(b), size);
                first = size - n;
                last = size - 1;
            }
            else
            {
                first = std::stoull(a);
                last = b.empty() ? size - 1 : std::min<unsigned long long>(std::stoull(b), size - 1);
            }
            if (size == 0 || first >= size || first > last)
            {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k416RequestedRangeNotSatisfiable);
                resp->addHeader("Content-Range", "bytes */" + std::to_string(size));
                return cb(resp);
            }
            offset = first;
            length = last - first + 1;
            partial = true;
        }
    }
    in.close();

    auto resp = partial ? HttpResponse::newFileResponse(path, offset, length, true, "", CT_CUSTOM, type, req)
                        : HttpResponse::newFileResponse(path, "", CT_CUSTOM, type, req);
    resp->addHeader("Accept-Ranges", "bytes");
    const auto name = req->getParameter("name");
    // Documents are only ever downloaded, never shown, so an uploaded .html
    // or .svg can't run in our origin.
    const bool save = document || req->getParameter("download") == "1";
    if (!name.empty() || save)
    {
        // RFC 5987 wants %20 for spaces, not "+" (a real "+" comes out as %2B).
        std::string disposition = save ? "attachment" : "inline";
        if (!name.empty())
        {
            std::string encoded = drogon::utils::urlEncodeComponent(files::cleanName(name, type));
            for (size_t p; (p = encoded.find('+')) != std::string::npos;)
                encoded.replace(p, 1, "%20");
            disposition += "; filename*=UTF-8''" + encoded;
        }
        resp->addHeader("Content-Disposition", disposition);
    }
    // The id is the content's hash, so it never changes.
    resp->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    resp->addHeader("X-Content-Type-Options", "nosniff");
    cb(resp);
}
