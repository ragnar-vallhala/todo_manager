#include "Files.h"

#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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

uint32_t be32(std::string_view d, size_t i)
{
    return static_cast<uint32_t>(be16(d, i)) << 16 | be16(d, i + 2);
}

uint64_t be64(std::string_view d, size_t i)
{
    return static_cast<uint64_t>(be32(d, i)) << 32 | be32(d, i + 4);
}

// What a video's headers say: its size in pixels (as shown, so turned for
// portrait phone videos) and its length in seconds; zeros where they don't.
struct VideoInfo
{
    int width{0}, height{0};
    double duration{0};
};

// MP4: the movie header (mvhd) has the length; the first track header
// (tkhd) with a size is the picture's, its matrix saying if it's turned.
void mp4Boxes(std::string_view d, size_t from, size_t to, VideoInfo &info)
{
    for (size_t i = from; i + 8 <= to;)
    {
        uint64_t size = be32(d, i);
        const auto type = d.substr(i + 4, 4);
        size_t header = 8;
        if (size == 1 && i + 16 <= to)
        {
            size = be64(d, i + 8);
            header = 16;
        }
        else if (size == 0)
            size = to - i;
        if (size < header || size > to - i)
            return;
        const size_t body = i + header, end = i + static_cast<size_t>(size);
        if (type == "moov" || type == "trak")
            mp4Boxes(d, body, end, info);
        else if (type == "mvhd" && end - body >= 32)
        {
            const bool v1 = d[body] == 1;
            const uint32_t scale = be32(d, body + (v1 ? 20 : 12));
            const uint64_t length = v1 ? be64(d, body + 24) : be32(d, body + 16);
            if (scale)
                info.duration = static_cast<double>(length) / scale;
        }
        else if (type == "tkhd" && !info.width && end - body >= 84)
        {
            const size_t m = body + (d[body] == 1 ? 52 : 40);  // the matrix
            if (m + 44 <= end)
            {
                const int w = static_cast<int>(be32(d, m + 36) >> 16);
                const int h = static_cast<int>(be32(d, m + 40) >> 16);
                // a == 0 and b != 0: turned a quarter.
                const bool turned = be32(d, m) == 0 && be32(d, m + 4) != 0;
                if (w > 0 && h > 0)
                {
                    info.width = turned ? h : w;
                    info.height = turned ? w : h;
                }
            }
        }
        i = end;
    }
}

// WebM (EBML): Segment > Info > Duration (in TimecodeScale units) and
// Segment > Tracks > TrackEntry > Video > PixelWidth / PixelHeight.
size_t ebmlLength(std::string_view d, size_t i, uint64_t &value, bool keepMarker)
{
    if (i >= d.size())
        return 0;
    const auto first = static_cast<unsigned char>(d[i]);
    size_t len = 1;
    while (len <= 8 && !(first & (0x80 >> (len - 1))))
        ++len;
    if (len > 8 || i + len > d.size())
        return 0;
    value = keepMarker ? first : first & (0xFF >> len);
    for (size_t k = 1; k < len; ++k)
        value = value << 8 | static_cast<unsigned char>(d[i + k]);
    return len;
}

void ebml(std::string_view d, size_t from, size_t to, VideoInfo &info, uint64_t &scale, double &rawDuration)
{
    for (size_t i = from; i < to;)
    {
        uint64_t id = 0, size = 0;
        const size_t idLen = ebmlLength(d, i, id, true);
        if (!idLen)
            return;
        const size_t sizeLen = ebmlLength(d, i + idLen, size, false);
        if (!sizeLen)
            return;
        const size_t body = i + idLen + sizeLen;
        // All ones: size unknown (streamed files); runs to the end of its parent.
        const bool unknown = size == (uint64_t(1) << (7 * sizeLen)) - 1;
        const size_t end = unknown || size > to - std::min(body, to) ? to : body + static_cast<size_t>(size);
        auto uint = [&] {
            uint64_t v = 0;
            for (size_t k = body; k < end && k < body + 8; ++k)
                v = v << 8 | static_cast<unsigned char>(d[k]);
            return v;
        };
        switch (id)
        {
        case 0x18538067:  // Segment
        case 0x1549A966:  // Info
        case 0x1654AE6B:  // Tracks
        case 0xAE:        // TrackEntry
        case 0xE0:        // Video
            ebml(d, body, end, info, scale, rawDuration);
            break;
        case 0x2AD7B1:  // TimecodeScale (ns)
            scale = uint();
            break;
        case 0x4489:  // Duration (float)
            if (end - body == 4)
            {
                const uint32_t bits = be32(d, body);
                float f;
                std::memcpy(&f, &bits, 4);
                rawDuration = f;
            }
            else if (end - body == 8)
            {
                const uint64_t bits = be64(d, body);
                double f;
                std::memcpy(&f, &bits, 8);
                rawDuration = f;
            }
            break;
        case 0xB0:  // PixelWidth
            if (!info.width)
                info.width = static_cast<int>(uint());
            break;
        case 0xBA:  // PixelHeight
            if (!info.height)
                info.height = static_cast<int>(uint());
            break;
        case 0x1F43B675:  // Cluster: the frames; the headers are all before.
            return;
        default:
            break;
        }
        if (unknown)
            return;
        i = end;
    }
}

VideoInfo videoInfo(std::string_view d, const std::string &type)
{
    VideoInfo info;
    if (type == "video/mp4")
        mp4Boxes(d, 0, d.size(), info);
    else if (type == "video/webm")
    {
        uint64_t scale = 1000000;
        double raw = 0;
        ebml(d, 0, d.size(), info, scale, raw);
        if (raw > 0)
            info.duration = raw * static_cast<double>(scale) / 1e9;
    }
    if (info.width <= 0 || info.height <= 0 || info.width > 100000 || info.height > 100000)
        info.width = info.height = 0;
    if (!(info.duration > 0 && info.duration < 1e7))
        info.duration = 0;
    return info;
}

const char *extension(const std::string &type)
{
    return type == "image/png"    ? ".png"
           : type == "image/jpeg" ? ".jpg"
           : type == "image/gif"  ? ".gif"
           : type == "image/webp" ? ".webp"
           : type == "video/mp4"  ? ".mp4"
           : type == "video/webm" ? ".webm"
           : type == "application/pdf" ? ".pdf"
                                  : "";
}

Attachment describe(const std::string &id, std::string_view data, const std::string &name)
{
    Attachment a;
    a.id = id;
    a.type = files::sniff(data);
    a.name = files::cleanName(name, a.type);
    if (a.type.empty())
        a.type = files::typeForName(a.name);
    a.size = static_cast<int64_t>(data.size());
    if (files::isVideo(a.type))
    {
        const auto v = videoInfo(data, a.type);
        a.width = v.width;
        a.height = v.height;
        a.duration = v.duration;
    }
    else
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
    // MP4 (ISO media): an "ftyp" box first. HEIF and AVIF images use it
    // too, and QuickTime is its own type, so go by the major brand.
    if (starts("ftyp", 4) && d.size() >= 12)
    {
        const auto brand = d.substr(8, 4);
        for (std::string_view no : {"heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1", "avif", "avis", "qt  ", "crx "})
            if (brand == no)
                return "";
        return "video/mp4";
    }
    // WebM: an EBML header whose DocType is "webm" (not other Matroska).
    if (starts("\x1A\x45\xDF\xA3") && d.substr(0, std::min<size_t>(d.size(), 64)).find("webm") != std::string_view::npos)
        return "video/webm";
    return "";
}

std::string kindOf(const std::string &type)
{
    return type.rfind("image/", 0) == 0 ? "image" : isVideo(type) ? "video" : "document";
}

std::string typeForName(const std::string &name)
{
    const auto dot = name.rfind('.');
    if (dot == std::string::npos)
        return "application/octet-stream";
    static const std::map<std::string, std::string> types = {
        {"txt", "text/plain"}, {"log", "text/plain"}, {"md", "text/markdown"}, {"csv", "text/csv"},
        {"json", "application/json"}, {"zip", "application/zip"}, {"gz", "application/gzip"},
        {"tar", "application/x-tar"}, {"7z", "application/x-7z-compressed"},
        {"apk", "application/vnd.android.package-archive"}, {"pdf", "application/pdf"},
        {"doc", "application/msword"}, {"xls", "application/vnd.ms-excel"}, {"ppt", "application/vnd.ms-powerpoint"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"odt", "application/vnd.oasis.opendocument.text"}, {"mp3", "audio/mpeg"}, {"wav", "audio/wav"},
        {"ogg", "audio/ogg"}, {"flac", "audio/flac"}, {"m4a", "audio/mp4"}, {"mov", "video/quicktime"},
        {"mkv", "video/x-matroska"}};
    const auto it = types.find(lower(name.substr(dot + 1)));
    return it == types.end() ? "application/octet-stream" : it->second;
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
    if (data.size() > kMaxDocumentSize)
        throw StoreError(413, "files can be at most 200 MB (50 MB for videos, 10 MB for images)");
    if (data.empty())
        throw StoreError(400, "the file is empty");
    const auto type = sniff(data);
    if (isVideo(type) && data.size() > kMaxVideoSize)
        throw StoreError(413, "videos can be at most 50 MB");
    if (type.rfind("image/", 0) == 0 && data.size() > kMaxSize)
        throw StoreError(413, "images can be at most 10 MB");

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
