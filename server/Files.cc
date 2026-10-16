#include "Files.h"

#include <drogon/utils/Utilities.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace fs = std::filesystem;

namespace
{
std::string gDir = "files";

// Ids are the first 12 hex digits of the SHA-256 of the content.
constexpr size_t kIdLength = 12;

std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool validId(const std::string &id)
{
    return id.size() == kIdLength && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}

unsigned be16(std::string_view d, size_t i)
{
    return static_cast<unsigned char>(d[i]) << 8 | static_cast<unsigned char>(d[i + 1]);
}

unsigned le16(std::string_view d, size_t i)
{
    return static_cast<unsigned char>(d[i]) | static_cast<unsigned char>(d[i + 1]) << 8;
}

unsigned le24(std::string_view d, size_t i)
{
    return le16(d, i) | static_cast<unsigned char>(d[i + 2]) << 16;
}

// An image's size in pixels from its header; 0 x 0 if it can't be read.
std::pair<int, int> dimensions(std::string_view d, const std::string &type)
{
    auto byte = [&](size_t i) { return static_cast<unsigned char>(d[i]); };
    if (type == "image/png" && d.size() >= 24)
        return {static_cast<int>(be16(d, 16) << 16 | be16(d, 18)),
                static_cast<int>(be16(d, 20) << 16 | be16(d, 22))};
    if (type == "image/gif" && d.size() >= 10)
        return {static_cast<int>(le16(d, 6)), static_cast<int>(le16(d, 8))};
    if (type == "image/jpeg")
    {
        // Walk the segments to the first frame header (SOFn).
        for (size_t i = 2; i + 9 < d.size();)
        {
            if (byte(i) != 0xFF)
                return {0, 0};
            const unsigned marker = byte(i + 1);
            if (marker == 0xFF || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9))
            {
                i += marker == 0xFF ? 1 : 2;
                continue;
            }
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
                return {static_cast<int>(be16(d, i + 7)), static_cast<int>(be16(d, i + 5))};
            i += 2 + be16(d, i + 2);
        }
        return {0, 0};
    }
    if (type == "image/webp" && d.size() >= 30)
    {
        const auto chunk = d.substr(12, 4);
        if (chunk == "VP8 ")
            return {static_cast<int>(le16(d, 26) & 0x3FFF), static_cast<int>(le16(d, 28) & 0x3FFF)};
        if (chunk == "VP8L")
            return {static_cast<int>(1 + ((byte(22) & 0x3F) << 8 | byte(21))),
                    static_cast<int>(1 + ((byte(24) & 0xF) << 10 | byte(23) << 2 | (byte(22) & 0xC0) >> 6))};
        if (chunk == "VP8X")
            return {static_cast<int>(1 + le24(d, 24)), static_cast<int>(1 + le24(d, 27))};
    }
    return {0, 0};
}

const char *extension(const std::string &type)
{
    return type == "image/png"    ? ".png"
           : type == "image/jpeg" ? ".jpg"
           : type == "image/gif"  ? ".gif"
           : type == "image/webp" ? ".webp"
                                  : ".pdf";
}

Attachment describe(const std::string &id, std::string_view data, const std::string &name)
{
    Attachment a;
    a.id = id;
    a.type = files::sniff(data);
    a.name = files::cleanName(name, a.type);
    a.size = static_cast<int64_t>(data.size());
    std::tie(a.width, a.height) = dimensions(data, a.type);
    return a;
}
}  // namespace

namespace files
{
void init(const std::string &dir)
{
    gDir = dir;
    fs::create_directories(gDir);
}

std::string sniff(std::string_view d)
{
    auto starts = [&](std::string_view magic, size_t at = 0) {
        return d.size() >= at + magic.size() && d.substr(at, magic.size()) == magic;
    };
    if (starts("\x89PNG\r\n\x1a\n"))
        return "image/png";
    if (starts("\xFF\xD8\xFF"))
        return "image/jpeg";
    if (starts("GIF87a") || starts("GIF89a"))
        return "image/gif";
    if (starts("RIFF") && starts("WEBP", 8))
        return "image/webp";
    if (starts("%PDF-"))
        return "application/pdf";
    return "";
}

std::string cleanName(const std::string &name, const std::string &type)
{
    std::string base = name.substr(name.find_last_of("/\\") + 1);
    std::string out;
    for (unsigned char c : base)
        if (c >= 0x20 && c != 0x7F)
            out += static_cast<char>(c);
    out.erase(0, out.find_first_not_of(' '));
    out.erase(out.find_last_not_of(' ') + 1);
    if (out.size() > 200)
    {
        // Cut on a character boundary, keeping the extension.
        const auto dot = out.rfind('.');
        const std::string ext = dot != std::string::npos && out.size() - dot <= 10 ? out.substr(dot) : "";
        size_t keep = 200 - ext.size();
        while (keep > 0 && (static_cast<unsigned char>(out[keep]) & 0xC0) == 0x80)
            --keep;
        out = out.substr(0, keep) + ext;
    }
    return out.empty() || out == "." || out == ".." ? std::string("file") + extension(type) : out;
}

Attachment store(std::string_view data, const std::string &name)
{
    if (data.size() > kMaxSize)
        throw StoreError(413, "files can be at most 10 MB");
    if (data.empty())
        throw StoreError(400, "the file is empty");
    const auto type = sniff(data);
    if (type.empty())
        throw StoreError(400, "only PNG, JPEG, GIF, WebP and PDF files can be attached");

    const std::string id = lower(drogon::utils::getSha256(data.data(), data.size())).substr(0, kIdLength);
    const std::string path = pathOf(id);
    if (!fs::exists(path))
    {
        // Write to a temp file, then rename, so a half-written file never
        // shows up under its id.
        const std::string tmp =
            path + ".tmp" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!out)
                throw StoreError(500, "couldn't save the file");
        }
        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (ec)
            throw StoreError(500, "couldn't save the file");
    }
    return describe(id, data, name);
}

std::optional<Attachment> find(const std::string &ref, const std::string &name)
{
    const std::string path = pathOf(ref);
    if (path.empty())
        return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return describe(fs::path(path).filename().string(), data, name);
}

std::string pathOf(const std::string &ref)
{
    // Clients show ids as "f" + the id; take that too.
    std::string id = lower(ref);
    if (id.size() == kIdLength + 1 && id[0] == 'f')
        id = id.substr(1);
    return validId(id) ? (fs::path(gDir) / id).string() : "";
}

std::string parseList(const Json::Value &v, std::vector<Attachment> &out)
{
    out.clear();
    if (v.isNull())
        return "";
    if (!v.isArray())
        return "attachments must be a list of {\"id\", \"name\"?}";
    if (v.size() > kMaxPerItem)
        return "at most 10 attachments";
    for (const auto &item : v)
    {
        const Json::Value &id = item.isObject() ? item["id"] : item;
        const Json::Value &name = item.isObject() ? item["name"] : Json::Value();
        if (!id.isString() || (!name.isNull() && !name.isString()))
            return "attachments must be a list of {\"id\", \"name\"?}";
        auto a = find(id.asString(), name.asString());
        if (!a)
            return "no file " + id.asString() + " (upload it first: POST /api/files)";
        out.push_back(std::move(*a));
    }
    return "";
}
}  // namespace files
