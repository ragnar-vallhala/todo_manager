// Uploaded files (images and PDFs) that messages and todos can attach.
// Each is stored once, under the data file's folder in files/, named by its
// content hash; the hash is also its id.
#pragma once

#include "TodoStore.h"

#include <optional>
#include <string>
#include <string_view>

namespace files
{
constexpr size_t kMaxSize = 10 * 1024 * 1024;       // images
constexpr size_t kMaxVideoSize = 50 * 1024 * 1024;  // MP4 and WebM
constexpr size_t kMaxDocumentSize = 200 * 1024 * 1024;  // everything else (PDFs, archives, APKs...)
constexpr size_t kMaxPerItem = 10;  // attachments on one message or todo

// Files go in `dir` (created if missing).
void init(const std::string &dir);

// "image/png", "image/jpeg", "image/gif", "image/webp", "application/pdf",
// "video/mp4" or "video/webm", going by the first bytes; "" for anything else
// (which is a document, typed by its file name: see typeForName).
// Only the first 64 bytes are looked at.
std::string sniff(std::string_view data);

// Stores `data` (once: the same bytes get the same id) and describes it,
// named `name`. Any bytes are fine; 413 if it's too big (10 MB for images,
// 50 MB for video, 200 MB for documents).
Attachment store(std::string_view data, const std::string &name);

// The stored file `id`, described (type, size, width and height read from
// the file itself), named `name`; nullopt if there's no such file.
std::optional<Attachment> find(const std::string &id, const std::string &name = "");

// Where file `id` is on disk ("f" + the id, as the clients show it, works
// too); "" if the id isn't one of ours.
std::string pathOf(const std::string &id);

// A content type for a document from its file name's extension, else
// "application/octet-stream". Types a browser would run (HTML, SVG, XML,
// scripts) come back as octet-stream.
std::string typeForName(const std::string &name);

// "image", "video" or "document" (anything else, PDFs included).
std::string kindOf(const std::string &type);

// True for the video types.
inline bool isVideo(const std::string &type) { return type.rfind("video/", 0) == 0; }

// A file name fit to show: no folders or control characters, at most 200
// bytes; "" becomes "file" plus the type's extension.
std::string cleanName(const std::string &name, const std::string &type);

// Reads [{"id": "...", "name"?: "..."}...] (or bare ids) into `out`,
// looking each one up. Returns an error message, or "" if they're fine.
std::string parseList(const Json::Value &v, std::vector<Attachment> &out);
}  // namespace files
