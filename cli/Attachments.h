// Files attached to messages and todos, as the clients see them, plus
// helpers for finding files to attach.
#pragma once

#include <json/json.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

struct AttachmentItem
{
    std::string id;  // shown as "f" + id
    std::string name;
    std::string type;  // "image/png", ..., "application/pdf", "video/mp4", "video/webm"
    int64_t size{0};
    int width{0}, height{0};  // 0 when unknown
    double duration{0};       // seconds, videos only; 0 when unknown

    bool image() const { return type.rfind("image/", 0) == 0; }
    bool video() const { return type.rfind("video/", 0) == 0; }
    std::string label() const { return "f" + id; }

    // "[image: shot.png 800x600, f1a2b3c4d5e6]", "[video: run.mp4 1280x720 0:42, f...]"
    // or "[file: notes.pdf, f...]".
    std::string marker() const
    {
        std::string dims = width && height ? " " + std::to_string(width) + "x" + std::to_string(height) : "";
        if (duration > 0)
        {
            const int s = static_cast<int>(duration + 0.5);
            const std::string sec = std::to_string(s % 60);
            dims += " " + std::to_string(s / 60) + ":" + (sec.size() < 2 ? "0" : "") + sec;
        }
        return std::string("[") + (image() ? "image: " : video() ? "video: " : "file: ") + name + dims + ", " +
               label() + "]";
    }

    static AttachmentItem fromJson(const Json::Value &v)
    {
        return {v["id"].asString(), v["name"].asString(), v["type"].asString(), v["size"].asInt64(),
                v["width"].asInt(), v["height"].asInt(), v["duration"].isNumeric() ? v["duration"].asDouble() : 0};
    }

    static std::vector<AttachmentItem> listFromJson(const Json::Value &v)
    {
        std::vector<AttachmentItem> out;
        for (const auto &a : v)
            out.push_back(fromJson(a));
        return out;
    }

    // [{"id", "name"}...], what the server takes.
    static Json::Value listToJson(const std::vector<AttachmentItem> &list)
    {
        Json::Value arr(Json::arrayValue);
        for (const auto &a : list)
        {
            Json::Value v;
            v["id"] = a.id;
            v["name"] = a.name;
            arr.append(v);
        }
        return arr;
    }
};

namespace attach
{
constexpr int64_t kMaxSize = 10 * 1024 * 1024;       // the server's limit for images and PDFs
constexpr int64_t kMaxVideoSize = 50 * 1024 * 1024;  // and for videos

// The most the server takes of a file of `type`.
inline int64_t maxFor(const std::string &type)
{
    return type.rfind("video/", 0) == 0 ? kMaxVideoSize : kMaxSize;
}

// "10 MB" or "50 MB".
inline std::string maxLabel(const std::string &type)
{
    return std::to_string(maxFor(type) / (1024 * 1024)) + " MB";
}

// The type of file `path` by its first bytes, as the server judges it: one of
// the types it takes, or "" (also when it can't be read or isn't a file).
inline std::string sniff(const std::string &path)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return "";
    std::ifstream in(path, std::ios::binary);
    char b[64] = {};
    in.read(b, sizeof(b));
    const std::string h(b, static_cast<size_t>(in.gcount()));
    if (h.rfind("\x89PNG\r\n\x1a\n", 0) == 0)
        return "image/png";
    if (h.rfind("\xFF\xD8\xFF", 0) == 0)
        return "image/jpeg";
    if (h.rfind("GIF87a", 0) == 0 || h.rfind("GIF89a", 0) == 0)
        return "image/gif";
    if (h.rfind("RIFF", 0) == 0 && h.size() >= 12 && h.compare(8, 4, "WEBP") == 0)
        return "image/webp";
    if (h.rfind("%PDF-", 0) == 0)
        return "application/pdf";
    // MP4: an ftyp box, unless its brand is an image's (HEIF, AVIF) or QuickTime.
    if (h.size() >= 12 && h.compare(4, 4, "ftyp") == 0)
    {
        const std::string brand = h.substr(8, 4);
        for (const char *no : {"heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1", "avif", "avis", "qt  ", "crx "})
            if (brand == no)
                return "";
        return "video/mp4";
    }
    if (h.rfind("\x1A\x45\xDF\xA3", 0) == 0 && h.find("webm") != std::string::npos)
        return "video/webm";
    return "";
}

// A path as pasted or dropped into a terminal: 'quoted', "quoted", with
// backslash escapes ("my\ file.png"), as a file:// URL, or starting with ~.
// Returns it as a plain path ("" if it's several lines); whether there's a
// file there is up to the caller.
inline std::string pathFromText(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    s.erase(0, s.find_first_not_of(" \n"));
    if (s.size() >= 2 && (s[0] == '\'' || s[0] == '"') && s.back() == s[0])
        s = s.substr(1, s.size() - 2);
    else if (s.find('\\') != std::string::npos)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
            out += s[i] == '\\' && i + 1 < s.size() ? s[++i] : s[i];
        s = out;
    }
    if (s.rfind("file://", 0) == 0)
    {
        // file:///home/... or file://host/home/...; %20 and friends decoded.
        const auto slash = s.find('/', 7);
        if (slash == std::string::npos)
            return "";
        std::string out;
        for (size_t i = slash; i < s.size(); ++i)
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(s[i + 2])))
            {
                out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            else
                out += s[i];
        s = out;
    }
    if (s.rfind("~/", 0) == 0)
        if (const char *home = std::getenv("HOME"))
            s = home + s.substr(1);
    return s.find('\n') == std::string::npos ? s : "";
}

// The file's last path component.
inline std::string baseName(const std::string &path)
{
    return path.substr(path.find_last_of('/') + 1);
}
}  // namespace attach
