#pragma once

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Ids are positive; 0 means "none" (a root category / an uncategorized todo).

// A file attached to a message or todo (see Files.h). The server fills in
// everything but the name from the stored file.
struct Attachment
{
    std::string id;    // the file's content hash
    std::string name;  // as uploaded, e.g. "screenshot.png"
    std::string type;  // "image/png", ..., "application/pdf", "video/mp4", "video/webm", or any other for documents
    int64_t size{0};   // bytes
    int width{0}, height{0};  // pixels, for images and videos whose header says; else 0
    double duration{0};       // seconds, for videos whose header says; else 0

    Json::Value toJson() const;
    static Attachment fromJson(const Json::Value &v);
};

// As a JSON array; items with none leave the key out, so files without
// attachments look as they did before.
Json::Value attachmentsJson(const std::vector<Attachment> &list);
std::vector<Attachment> readAttachments(const Json::Value &v);

struct Todo
{
    int64_t id{0};
    std::string title;
    bool done{false};
    int64_t createdAt{0};  // unix seconds
    int64_t categoryId{0};
    int priority{0};       // 0 none, 1 low, 2 medium, 3 high
    std::string deadline;  // "YYYY-MM-DD", or "" for none
    std::string description;  // optional notes, may span lines; "" for none
    // Unix seconds when it was pruned; 0 for live todos. Pruned todos have
    // their own ids (shown as p1, p2...), so their old id is free again.
    int64_t prunedAt{0};
    int64_t roomId{0};      // the room it's shared in; 0 for none
    std::string claimedBy;  // the agent working on it; "" for nobody
    std::vector<Attachment> attachments;  // files that go with the description

    Json::Value toJson() const;
};

// Priority names used in the API ("low", "medium", "high"); "" is none.
std::string priorityName(int priority);
// -1 if `name` isn't one of them.
int parsePriority(const std::string &name);
// True for a real calendar date written as YYYY-MM-DD.
bool isValidDate(const std::string &date);

// Changes to a todo; unset fields are left alone.
struct TodoPatch
{
    std::optional<std::string> title;
    std::optional<bool> done;
    std::optional<int64_t> categoryId;
    std::optional<int> priority;
    std::optional<std::string> deadline;     // "" clears it
    std::optional<std::string> description;  // "" clears it
    std::optional<int64_t> roomId;           // 0 takes it out of its room
    std::optional<std::vector<Attachment>> attachments;  // replaces the list

    bool empty() const
    {
        return !title && !done && !categoryId && !priority && !deadline && !description &&
               !roomId && !attachments;
    }
};

// Colors are "#rrggbb", or "" for the viewer's usual color.
struct CategoryStyle
{
    std::string fg, bg;
    bool bold{false}, italic{false}, underline{false};

    bool empty() const { return fg.empty() && bg.empty() && !bold && !italic && !underline; }
    Json::Value toJson() const;  // null when empty
    // Reads {"fg"?, "bg"?, "bold"?, "italic"?, "underline"?} or null; returns
    // an error message, or "" if it's fine.
    static std::string fromJson(const Json::Value &v, CategoryStyle &out);
};

struct Category
{
    int64_t id{0};
    std::string name;
    int64_t parentId{0};
    int64_t createdAt{0};
    // Archived categories, their subcategories and their todos are left out
    // of listings unless asked for, and kept by "clear completed".
    bool archived{false};
    // How the category's name and tags look, so categories are easy to tell
    // apart. Subcategories without their own use their parent's.
    CategoryStyle style;

    Json::Value toJson() const;
};

// A place where agents (and people) coordinate: a message log plus the todos
// shared in it, which they claim before working on them.
struct Room
{
    int64_t id{0};
    std::string name;
    std::string topic;  // what the room is for; "" for none
    int64_t createdAt{0};
    // Agent name -> unix seconds it last posted, claimed or read here.
    std::map<std::string, int64_t> members;
    // Goes up whenever messages leave or rejoin the log (prune, restore),
    // so clients holding a copy know to load it again.
    int64_t logVersion{0};

    Json::Value toJson() const;
};

// Like todos, a message takes the lowest id free in its room, and pruning
// moves it aside (with a pN id), freeing its id. Logs are ordered by `seq`,
// which only grows (one counter for every room), so "messages after seq N"
// works as a cursor.
struct Message
{
    int64_t id{0};
    int64_t seq{0};
    int64_t roomId{0};
    std::string author;  // "" for the server
    std::string text;
    int64_t createdAt{0};
    // "message" is something someone said; "event" is written by the server
    // when a room todo changes (added, claimed, done...), naming that todo.
    std::string kind{"message"};
    int64_t todoId{0};
    int64_t prunedAt{0};  // unix seconds it was pruned; 0 in the live log
    std::vector<Attachment> attachments;

    Json::Value toJson() const;
};

// An attachment in a room's log, with the message it's on.
struct RoomFile
{
    Attachment file;
    int64_t messageId{0};
    int64_t seq{0};
    std::string author;
    int64_t createdAt{0};

    Json::Value toJson() const;
};

// A request the store refuses; `status` is the HTTP status to report.
struct StoreError : std::runtime_error
{
    StoreError(int status, const std::string &msg)
        : std::runtime_error(msg), status(status)
    {
    }
    int status;
};

// Thread-safe in-memory todos and categories, persisted to a JSON file.
class TodoStore
{
  public:
    static void init(const std::string &path);
    static TodoStore &get();

    // Without `includeArchived`, todos in archived categories are left out.
    std::vector<Todo> list(bool includeArchived) const;
    // `actor` (an agent name, or "") is who made the change; changes to room
    // todos are announced in the room under that name.

    // Takes the category, room, priority, deadline, description and
    // attachments from `init`.
    Todo add(const std::string &title, const Todo &init, const std::string &actor = "");
    std::optional<Todo> update(int64_t id, const TodoPatch &patch, const std::string &actor = "");
    bool remove(int64_t id, const std::string &actor = "");
    // Claiming is how agents split up work: 409 if someone else holds it,
    // unless `force`. Releasing someone else's claim also needs `force`.
    Todo claim(int64_t id, const std::string &agent, bool force);
    Todo release(int64_t id, const std::string &agent, bool force);
    size_t clearDone();  // keeps todos in archived categories

    // Pruning moves a done todo to the pruned list, freeing its id: new todos
    // take the lowest free id, so ids stay small.
    std::vector<Todo> listPruned() const;
    Todo prune(int64_t id);  // 404 if there's no such todo, 409 if it isn't done
    size_t pruneDone();      // every done todo, except in archived categories
    Todo restore(int64_t prunedId);  // back as a done todo with a free id
    bool removePruned(int64_t prunedId);
    size_t clearPruned();

    // Without `includeArchived`, archived categories (and everything below
    // them) are left out.
    std::vector<Category> listCategories(bool includeArchived) const;
    Category addCategory(const std::string &name, int64_t parentId);
    Category updateCategory(int64_t id,
                            const std::optional<std::string> &name,
                            std::optional<int64_t> parentId,
                            std::optional<bool> archived,
                            const std::optional<CategoryStyle> &style);
    // Deletes a category; its todos and subcategories move to its parent.
    void removeCategory(int64_t id);

    // ── Rooms ──
    std::vector<Room> listRooms() const;
    Room room(int64_t id) const;  // 404 if missing
    // Finds a room by id ("3", "#3") or name (case-insensitive); 404 if none.
    Room findRoom(const std::string &ref) const;
    Room addRoom(const std::string &name, const std::string &topic);
    Room updateRoom(int64_t id, const std::optional<std::string> &name,
                    const std::optional<std::string> &topic);
    // Deletes the room and its messages; its todos stay, outside any room.
    void removeRoom(int64_t id);
    // Drops `name` from the room's members (they rejoin if they show up again).
    Room removeMember(int64_t id, const std::string &name);
    // `text` may be empty when there are attachments.
    Message post(int64_t roomId, const std::string &author, const std::string &text,
                 const std::vector<Attachment> &attachments = {});
    // Messages with seq above `since`, oldest first, at most `limit` (the
    // newest ones if there are more). since 0 = from the start.
    // The newest `limit` messages with seq after `since` (and before
    // `before`, if that's set), oldest first.
    std::vector<Message> messages(int64_t roomId, int64_t since, size_t limit, int64_t before = 0) const;
    // The room's attachments, newest first, from messages before seq
    // `before` (0: from the newest); about `limit` of them (a message's
    // files aren't split).
    std::vector<RoomFile> files(int64_t roomId, int64_t before, size_t limit) const;
    // The newest message's seq, the cursor to read or wait after; 0 if none.
    int64_t cursor(int64_t roomId) const;

    // Pruned messages leave the log but are kept, with ids pN of their own,
    // until restored or removed. 404 if the room or message doesn't exist.
    Message pruneMessage(int64_t roomId, int64_t id);
    // Prunes all but the newest `keep` messages; returns how many.
    size_t pruneMessages(int64_t roomId, size_t keep);
    std::vector<Message> listPrunedMessages(int64_t roomId) const;  // oldest first
    size_t prunedMessageCount(int64_t roomId) const;
    Message restoreMessage(int64_t roomId, int64_t prunedId);  // back, with a free id
    bool removePrunedMessage(int64_t roomId, int64_t prunedId);
    size_t clearPrunedMessages(int64_t roomId);
    // Notes that `agent` looked at the room (kept in memory until the next save).
    void touch(int64_t roomId, const std::string &agent);

    // Called with the room id after each new message, while the store is
    // locked: it must not call back into the store.
    void setMessageListener(std::function<void(int64_t)> fn);

  private:
    explicit TodoStore(std::string path);
    void load();
    void save() const;  // callers below must hold mu_

    Category *findCategory(int64_t id);
    const Category *findCategory(int64_t id) const;
    // True if category `id` or one of its ancestors is archived.
    bool inArchive(int64_t id) const;
    void checkCategoryExists(int64_t id);
    void checkSiblingName(const std::string &name, int64_t parentId, int64_t selfId);
    Room *findRoomById(int64_t id);
    void checkRoomExists(int64_t id);
    void checkRoomName(const std::string &name, int64_t selfId);
    // Moves a live message to the pruned list; callers save.
    void pruneAt(std::vector<Message>::iterator it, int64_t now);
    // Appends a message (and marks `author` as a member); callers save.
    const Message &append(int64_t roomId, const std::string &author, const std::string &text,
                          const std::string &kind, int64_t todoId,
                          const std::vector<Attachment> &attachments = {});
    // Announces a change to a room todo in its room, if it has one.
    void event(const Todo &t, const std::string &actor, const std::string &what);

    mutable std::mutex mu_;
    std::string path_;
    std::vector<Todo> todos_;
    std::vector<Category> categories_;
    std::vector<Todo> pruned_;
    std::vector<Room> rooms_;
    std::vector<Message> messages_;        // by seq
    std::vector<Message> prunedMessages_;  // by seq
    int64_t nextCategoryId_{1};
    int64_t nextRoomId_{1};
    int64_t nextSeq_{1};
    std::function<void(int64_t)> listener_;
};
