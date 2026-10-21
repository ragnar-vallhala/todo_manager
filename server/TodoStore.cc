#include "TodoStore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>

namespace
{
std::unique_ptr<TodoStore> gStore;

int64_t nowSeconds()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch())
        .count();
}

// The lowest positive id none of `items` has (only those `in` accepts).
template <typename T, typename In>
int64_t freeId(const std::vector<T> &items, In in)
{
    std::vector<int64_t> ids;
    for (const auto &t : items)
        if (in(t))
            ids.push_back(t.id);
    std::sort(ids.begin(), ids.end());
    int64_t id = 1;
    for (int64_t used : ids)
        if (used == id)
            ++id;
        else if (used > id)
            break;
    return id;
}

int64_t freeId(const std::vector<Todo> &todos)
{
    return freeId(todos, [](const Todo &) { return true; });
}

// The lowest id free among room `roomId`'s messages in `msgs`.
int64_t freeMessageId(const std::vector<Message> &msgs, int64_t roomId)
{
    return freeId(msgs, [roomId](const Message &m) { return m.roomId == roomId; });
}

// Keeps `msgs` ordered by seq.
void insertBySeq(std::vector<Message> &msgs, Message m)
{
    auto at = std::upper_bound(msgs.begin(), msgs.end(), m.seq,
                               [](int64_t seq, const Message &x) { return seq < x.seq; });
    msgs.insert(at, std::move(m));
}

// Ids are stored as JSON null when 0 ("none").
Json::Value idOrNull(int64_t id)
{
    return id ? Json::Value(static_cast<Json::Int64>(id)) : Json::Value();
}

constexpr const char *kPriorities[] = {"", "low", "medium", "high"};

std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Older messages are dropped past this many per room.
constexpr size_t kMaxMessagesPerRoom = 5000;

std::string label(const Todo &t)
{
    return "#" + std::to_string(t.id) + " " + t.title;
}
}  // namespace

std::string priorityName(int priority)
{
    return priority >= 1 && priority <= 3 ? kPriorities[priority] : "";
}

int parsePriority(const std::string &name)
{
    for (int i = 1; i <= 3; ++i)
        if (name == kPriorities[i])
            return i;
    return -1;
}

bool isValidDate(const std::string &date)
{
    int y, m, d;
    char extra;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-' ||
        std::sscanf(date.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3)
        return false;
    if (m < 1 || m > 12 || d < 1)
        return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return d <= days[m - 1] + (m == 2 && leap);
}

Json::Value Attachment::toJson() const
{
    Json::Value v;
    v["id"] = id;
    v["name"] = name;
    v["type"] = type;
    v["kind"] = type.rfind("image/", 0) == 0 ? "image" : type.rfind("video/", 0) == 0 ? "video" : "document";
    v["size"] = static_cast<Json::Int64>(size);
    if (width && height)
    {
        v["width"] = width;
        v["height"] = height;
    }
    if (duration > 0)
        v["duration"] = std::round(duration * 1000) / 1000;
    return v;
}

Attachment Attachment::fromJson(const Json::Value &v)
{
    Attachment a;
    a.id = v["id"].asString();
    a.name = v["name"].asString();
    a.type = v["type"].asString();
    a.size = v["size"].asInt64();
    a.width = v["width"].asInt();
    a.height = v["height"].asInt();
    a.duration = v["duration"].isNumeric() ? v["duration"].asDouble() : 0;
    return a;
}

Json::Value attachmentsJson(const std::vector<Attachment> &list)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &a : list)
        arr.append(a.toJson());
    return arr;
}

std::vector<Attachment> readAttachments(const Json::Value &v)
{
    std::vector<Attachment> out;
    if (v.isArray())
        for (const auto &a : v)
            if (a.isObject() && a["id"].isString())
                out.push_back(Attachment::fromJson(a));
    return out;
}

Json::Value Todo::toJson() const
{
    Json::Value v;
    v["id"] = static_cast<Json::Int64>(id);
    v["title"] = title;
    v["done"] = done;
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    v["categoryId"] = idOrNull(categoryId);
    v["priority"] = priority ? Json::Value(priorityName(priority)) : Json::Value();
    v["deadline"] = deadline.empty() ? Json::Value() : Json::Value(deadline);
    v["description"] = description.empty() ? Json::Value() : Json::Value(description);
    if (prunedAt)
        v["prunedAt"] = static_cast<Json::Int64>(prunedAt);
    v["roomId"] = idOrNull(roomId);
    v["claimedBy"] = claimedBy.empty() ? Json::Value() : Json::Value(claimedBy);
    if (!attachments.empty())
        v["attachments"] = attachmentsJson(attachments);
    return v;
}

Json::Value Room::toJson() const
{
    Json::Value v;
    v["id"] = static_cast<Json::Int64>(id);
    v["name"] = name;
    v["topic"] = topic.empty() ? Json::Value() : Json::Value(topic);
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    v["members"] = Json::Value(Json::arrayValue);
    for (const auto &[agent, seen] : members)
    {
        Json::Value m;
        m["name"] = agent;
        m["lastSeen"] = static_cast<Json::Int64>(seen);
        v["members"].append(m);
    }
    v["logVersion"] = static_cast<Json::Int64>(logVersion);
    v["archived"] = archived;
    return v;
}

Json::Value Message::toJson() const
{
    Json::Value v;
    v["id"] = static_cast<Json::Int64>(id);
    v["seq"] = static_cast<Json::Int64>(seq);
    v["roomId"] = static_cast<Json::Int64>(roomId);
    v["author"] = author.empty() ? Json::Value() : Json::Value(author);
    v["text"] = text;
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    v["kind"] = kind;
    v["todoId"] = idOrNull(todoId);
    if (prunedAt)
        v["prunedAt"] = static_cast<Json::Int64>(prunedAt);
    if (!attachments.empty())
        v["attachments"] = attachmentsJson(attachments);
    return v;
}

Json::Value CategoryStyle::toJson() const
{
    if (empty())
        return Json::Value();
    Json::Value v(Json::objectValue);
    if (!fg.empty())
        v["fg"] = fg;
    if (!bg.empty())
        v["bg"] = bg;
    for (auto [key, on] : {std::pair{"bold", bold}, {"italic", italic}, {"underline", underline}})
        if (on)
            v[key] = true;
    return v;
}

std::string CategoryStyle::fromJson(const Json::Value &v, CategoryStyle &out)
{
    out = CategoryStyle();
    if (v.isNull())
        return "";
    if (!v.isObject())
        return "style must be an object or null";
    auto isHex = [](const std::string &s) {
        return s.size() == 7 && s[0] == '#' &&
               s.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
    };
    for (auto [key, color] : {std::pair{"fg", &out.fg}, {"bg", &out.bg}})
    {
        const auto &c = v[key];
        if (c.isNull())
            continue;
        if (!c.isString() || !isHex(c.asString()))
            return std::string("style.") + key + " must be a color like \"#e06c75\", or null";
        *color = lower(c.asString());
    }
    for (auto [key, flag] :
         {std::pair{"bold", &out.bold}, {"italic", &out.italic}, {"underline", &out.underline}})
    {
        const auto &b = v[key];
        if (b.isNull())
            continue;
        if (!b.isBool())
            return std::string("style.") + key + " must be true or false";
        *flag = b.asBool();
    }
    return "";
}

Json::Value Category::toJson() const
{
    Json::Value v;
    v["id"] = static_cast<Json::Int64>(id);
    v["name"] = name;
    v["parentId"] = idOrNull(parentId);
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    v["archived"] = archived;
    v["style"] = style.toJson();
    return v;
}

void TodoStore::init(const std::string &path)
{
    gStore.reset(new TodoStore(path));
}

TodoStore &TodoStore::get()
{
    if (!gStore)
        throw std::logic_error("TodoStore::init() was not called");
    return *gStore;
}

TodoStore::TodoStore(std::string path) : path_(std::move(path))
{
    load();
}

void TodoStore::load()
{
    std::ifstream in(path_);
    if (!in)
        return;  // first run: start empty

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs))
        throw std::runtime_error("Invalid data file " + path_ + ": " + errs);

    // Older data files were a bare array of todos.
    const Json::Value todos = root.isArray() ? root : root["todos"];
    const Json::Value cats = root.isObject() ? root["categories"] : Json::Value();
    const Json::Value pruned = root.isObject() ? root["pruned"] : Json::Value();
    if (!todos.isNull() && !todos.isArray())
        throw std::runtime_error("Invalid data file " + path_);

    auto readTodo = [](const Json::Value &v) {
        Todo t;
        t.id = v["id"].asInt64();
        t.title = v["title"].asString();
        t.done = v["done"].asBool();
        t.createdAt = v["createdAt"].asInt64();
        t.categoryId = v["categoryId"].isNull() ? 0 : v["categoryId"].asInt64();
        t.priority = std::max(0, parsePriority(v["priority"].asString()));
        if (isValidDate(v["deadline"].asString()))
            t.deadline = v["deadline"].asString();
        t.description = v["description"].isString() ? v["description"].asString() : "";
        t.prunedAt = v["prunedAt"].asInt64();
        t.roomId = v["roomId"].isNull() ? 0 : v["roomId"].asInt64();
        t.claimedBy = v["claimedBy"].isString() ? v["claimedBy"].asString() : "";
        t.attachments = readAttachments(v["attachments"]);
        return t;
    };
    for (const auto &v : todos)
        todos_.push_back(readTodo(v));
    for (const auto &v : pruned)
        pruned_.push_back(readTodo(v));
    for (const auto &v : cats)
    {
        Category c;
        c.id = v["id"].asInt64();
        c.name = v["name"].asString();
        c.parentId = v["parentId"].isNull() ? 0 : v["parentId"].asInt64();
        c.createdAt = v["createdAt"].asInt64();
        c.archived = v["archived"].asBool();
        CategoryStyle::fromJson(v["style"], c.style);  // a bad one just stays empty
        nextCategoryId_ = std::max(nextCategoryId_, c.id + 1);
        categories_.push_back(std::move(c));
    }
    for (const auto &v : root.isObject() ? root["rooms"] : Json::Value())
    {
        Room r;
        r.id = v["id"].asInt64();
        r.name = v["name"].asString();
        r.topic = v["topic"].isString() ? v["topic"].asString() : "";
        r.createdAt = v["createdAt"].asInt64();
        for (const auto &m : v["members"])
            r.members[m["name"].asString()] = m["lastSeen"].asInt64();
        r.logVersion = v["logVersion"].asInt64();
        r.archived = v["archived"].asBool();  // missing in old files: false
        for (const auto &s : v["archivedSeqs"])
            r.archivedSeqs.push_back(s.asInt64());
        nextRoomId_ = std::max(nextRoomId_, r.id + 1);
        rooms_.push_back(std::move(r));
    }
    for (const char *key : {"messages", "prunedMessages"})
        for (const auto &v : root.isObject() ? root[key] : Json::Value())
        {
            Message m;
            m.id = v["id"].asInt64();
            // Before seq, ids were global and only grew, so they serve as one.
            m.seq = v.isMember("seq") ? v["seq"].asInt64() : m.id;
            m.roomId = v["roomId"].asInt64();
            m.author = v["author"].isString() ? v["author"].asString() : "";
            m.text = v["text"].asString();
            m.createdAt = v["createdAt"].asInt64();
            m.kind = v["kind"].asString() == "event" ? "event" : "message";
            m.todoId = v["todoId"].isNull() ? 0 : v["todoId"].asInt64();
            m.prunedAt = v["prunedAt"].asInt64();
            m.attachments = readAttachments(v["attachments"]);
            nextSeq_ = std::max(nextSeq_, m.seq + 1);
            insertBySeq(m.prunedAt ? prunedMessages_ : messages_, std::move(m));
        }
}

void TodoStore::save() const
{
    Json::Value root;
    root["todos"] = Json::Value(Json::arrayValue);
    root["categories"] = Json::Value(Json::arrayValue);
    root["pruned"] = Json::Value(Json::arrayValue);
    root["rooms"] = Json::Value(Json::arrayValue);
    root["messages"] = Json::Value(Json::arrayValue);
    root["prunedMessages"] = Json::Value(Json::arrayValue);
    for (const auto &r : rooms_)
    {
        Json::Value rv = r.toJson();
        if (!r.archivedSeqs.empty())
        {
            rv["archivedSeqs"] = Json::Value(Json::arrayValue);
            for (auto s : r.archivedSeqs)
                rv["archivedSeqs"].append(static_cast<Json::Int64>(s));
        }
        root["rooms"].append(rv);
    }
    for (const auto &m : messages_)
        root["messages"].append(m.toJson());
    for (const auto &m : prunedMessages_)
        root["prunedMessages"].append(m.toJson());
    for (const auto &t : todos_)
        root["todos"].append(t.toJson());
    for (const auto &t : pruned_)
        root["pruned"].append(t.toJson());
    for (const auto &c : categories_)
        root["categories"].append(c.toJson());

    // Write to a temp file, then rename, so a crash never leaves a
    // half-written data file behind.
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        Json::StreamWriterBuilder w;
        w["indentation"] = "  ";
        w["precision"] = 3;
        w["precisionType"] = "decimal";
        out << Json::writeString(w, root) << '\n';
        if (!out)
            throw std::runtime_error("Failed writing " + tmp);
    }
    if (std::rename(tmp.c_str(), path_.c_str()) != 0)
        throw std::runtime_error("Failed renaming " + tmp + " -> " + path_);
}

// ── Todos ──

std::vector<Todo> TodoStore::list(bool includeArchived) const
{
    std::lock_guard<std::mutex> lock(mu_);
    if (includeArchived)
        return todos_;
    std::vector<Todo> out;
    for (const auto &t : todos_)
        if (!inArchive(t.categoryId))
            out.push_back(t);
    return out;
}

Todo TodoStore::add(const std::string &title, const Todo &init, const std::string &actor)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkCategoryExists(init.categoryId);
    checkRoomExists(init.roomId);
    Todo t{freeId(todos_), title, false, nowSeconds(), init.categoryId, init.priority, init.deadline,
           init.description};
    t.roomId = init.roomId;
    t.attachments = init.attachments;
    todos_.push_back(t);
    event(t, actor, "added");
    save();
    return t;
}

std::optional<Todo> TodoStore::update(int64_t id, const TodoPatch &patch,
                                      const std::string &actor)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) {
        return t.id == id;
    });
    if (it == todos_.end())
        return std::nullopt;
    if (patch.categoryId)
        checkCategoryExists(*patch.categoryId);
    if (patch.roomId)
        checkRoomExists(*patch.roomId);
    const Todo before = *it;
    if (patch.title)
        it->title = *patch.title;
    if (patch.done)
        it->done = *patch.done;
    if (patch.categoryId)
        it->categoryId = *patch.categoryId;
    if (patch.priority)
        it->priority = *patch.priority;
    if (patch.deadline)
        it->deadline = *patch.deadline;
    if (patch.description)
        it->description = *patch.description;
    if (patch.roomId)
        it->roomId = *patch.roomId;
    if (patch.attachments)
        it->attachments = *patch.attachments;

    if (it->roomId != before.roomId)
    {
        event(before, actor, "moved out of this room:");
        event(*it, actor, "added");
    }
    else if (it->done != before.done)
        event(*it, actor, it->done ? "finished" : "reopened");
    save();
    return *it;
}

bool TodoStore::remove(int64_t id, const std::string &actor)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) {
        return t.id == id;
    });
    if (it == todos_.end())
        return false;
    event(*it, actor, "deleted");
    todos_.erase(it);
    save();
    return true;
}

Todo TodoStore::claim(int64_t id, const std::string &agent, bool force)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) { return t.id == id; });
    if (it == todos_.end())
        throw StoreError(404, "todo not found");
    if (it->claimedBy == agent)
        return *it;
    if (!it->claimedBy.empty() && !force)
        throw StoreError(409, "already claimed by " + it->claimedBy);
    const std::string previous = it->claimedBy;
    it->claimedBy = agent;
    event(*it, agent, previous.empty() ? "claimed" : "took over from " + previous + ":");
    if (it->roomId)
        if (Room *r = findRoomById(it->roomId))
            r->members[agent] = nowSeconds();
    save();
    return *it;
}

Todo TodoStore::release(int64_t id, const std::string &agent, bool force)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) { return t.id == id; });
    if (it == todos_.end())
        throw StoreError(404, "todo not found");
    if (it->claimedBy.empty())
        return *it;
    if (it->claimedBy != agent && !force)
        throw StoreError(409, "claimed by " + it->claimedBy + ", not you");
    const std::string previous = it->claimedBy;
    it->claimedBy.clear();
    event(*it, agent, previous == agent ? "released" : "released " + previous + "'s claim on");
    save();
    return *it;
}

size_t TodoStore::clearDone()
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::remove_if(todos_.begin(), todos_.end(), [this](const Todo &t) {
        return t.done && !inArchive(t.categoryId);
    });
    size_t n = std::distance(it, todos_.end());
    if (n > 0)
    {
        todos_.erase(it, todos_.end());
        save();
    }
    return n;
}

// ── Pruned ──

std::vector<Todo> TodoStore::listPruned() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return pruned_;
}

Todo TodoStore::prune(int64_t id)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) { return t.id == id; });
    if (it == todos_.end())
        throw StoreError(404, "todo not found");
    if (!it->done)
        throw StoreError(409, "only done todos can be pruned");
    Todo p = *it;
    p.id = freeId(pruned_);
    p.prunedAt = nowSeconds();
    p.claimedBy.clear();
    todos_.erase(it);
    pruned_.push_back(p);
    save();
    return p;
}

size_t TodoStore::pruneDone()
{
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    const int64_t now = nowSeconds();
    for (auto it = todos_.begin(); it != todos_.end();)
    {
        if (!it->done || inArchive(it->categoryId))
        {
            ++it;
            continue;
        }
        Todo p = *it;
        p.id = freeId(pruned_);
        p.prunedAt = now;
        p.claimedBy.clear();
        pruned_.push_back(p);
        it = todos_.erase(it);
        ++n;
    }
    if (n)
        save();
    return n;
}

Todo TodoStore::restore(int64_t prunedId)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(pruned_.begin(), pruned_.end(),
                           [prunedId](const Todo &t) { return t.id == prunedId; });
    if (it == pruned_.end())
        throw StoreError(404, "pruned todo not found");
    Todo t = *it;
    t.id = freeId(todos_);
    t.prunedAt = 0;
    if (!findRoomById(t.roomId))
        t.roomId = 0;
    pruned_.erase(it);
    todos_.push_back(t);
    save();
    return t;
}

bool TodoStore::removePruned(int64_t prunedId)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(pruned_.begin(), pruned_.end(),
                           [prunedId](const Todo &t) { return t.id == prunedId; });
    if (it == pruned_.end())
        return false;
    pruned_.erase(it);
    save();
    return true;
}

size_t TodoStore::clearPruned()
{
    std::lock_guard<std::mutex> lock(mu_);
    const size_t n = pruned_.size();
    if (n)
    {
        pruned_.clear();
        save();
    }
    return n;
}

// ── Categories ──

Category *TodoStore::findCategory(int64_t id)
{
    for (auto &c : categories_)
        if (c.id == id)
            return &c;
    return nullptr;
}

const Category *TodoStore::findCategory(int64_t id) const
{
    return const_cast<TodoStore *>(this)->findCategory(id);
}

bool TodoStore::inArchive(int64_t id) const
{
    for (const Category *c = findCategory(id); c; c = findCategory(c->parentId))
        if (c->archived)
            return true;
    return false;
}

void TodoStore::checkCategoryExists(int64_t id)
{
    if (id != 0 && !findCategory(id))
        throw StoreError(404, "category not found");
}

void TodoStore::checkSiblingName(const std::string &name, int64_t parentId, int64_t selfId)
{
    const auto key = lower(name);
    for (const auto &c : categories_)
        if (c.parentId == parentId && c.id != selfId && lower(c.name) == key)
            throw StoreError(409, "a category named \"" + name + "\" already exists there" +
                                      (inArchive(c.id) ? " (archived)" : ""));
}

std::vector<Category> TodoStore::listCategories(bool includeArchived) const
{
    std::lock_guard<std::mutex> lock(mu_);
    if (includeArchived)
        return categories_;
    std::vector<Category> out;
    for (const auto &c : categories_)
        if (!inArchive(c.id))
            out.push_back(c);
    return out;
}

Category TodoStore::addCategory(const std::string &name, int64_t parentId)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkCategoryExists(parentId);
    checkSiblingName(name, parentId, 0);
    Category c{nextCategoryId_++, name, parentId, nowSeconds()};
    categories_.push_back(c);
    save();
    return c;
}

Category TodoStore::updateCategory(int64_t id,
                                   const std::optional<std::string> &name,
                                   std::optional<int64_t> parentId,
                                   std::optional<bool> archived,
                                   const std::optional<CategoryStyle> &style)
{
    std::lock_guard<std::mutex> lock(mu_);
    Category *cat = findCategory(id);
    if (!cat)
        throw StoreError(404, "category not found");

    const int64_t newParent = parentId.value_or(cat->parentId);
    if (parentId)
    {
        checkCategoryExists(newParent);
        // Walk up from the new parent: meeting `id` would create a loop.
        for (int64_t p = newParent; p != 0; p = findCategory(p)->parentId)
            if (p == id)
                throw StoreError(400, "can't move a category inside itself");
    }
    checkSiblingName(name.value_or(cat->name), newParent, id);

    if (name)
        cat->name = *name;
    cat->parentId = newParent;
    if (archived)
        cat->archived = *archived;
    if (style)
        cat->style = *style;
    save();
    return *cat;
}

void TodoStore::removeCategory(int64_t id)
{
    std::lock_guard<std::mutex> lock(mu_);
    Category *cat = findCategory(id);
    if (!cat)
        throw StoreError(404, "category not found");
    const int64_t parent = cat->parentId;

    // Children keep their names, so refuse if one would clash with a sibling
    // it's about to join.
    for (const auto &child : categories_)
        if (child.parentId == id)
            for (const auto &other : categories_)
                if (other.parentId == parent && other.id != id &&
                    lower(other.name) == lower(child.name))
                    throw StoreError(409, "can't delete: subcategory \"" + child.name +
                                              "\" would clash with an existing one");

    for (auto &c : categories_)
        if (c.parentId == id)
            c.parentId = parent;
    for (auto *list : {&todos_, &pruned_})
        for (auto &t : *list)
            if (t.categoryId == id)
                t.categoryId = parent;
    categories_.erase(std::remove_if(categories_.begin(), categories_.end(),
                                     [id](const Category &c) { return c.id == id; }),
                      categories_.end());
    save();
}

// ── Rooms ──

Room *TodoStore::findRoomById(int64_t id)
{
    for (auto &r : rooms_)
        if (r.id == id)
            return &r;
    return nullptr;
}

void TodoStore::checkRoomExists(int64_t id)
{
    if (id != 0 && !findRoomById(id))
        throw StoreError(404, "room not found");
}

void TodoStore::checkRoomName(const std::string &name, int64_t selfId)
{
    const auto key = lower(name);
    for (const auto &r : rooms_)
        if (r.id != selfId && lower(r.name) == key)
            throw StoreError(409, "a room named \"" + name + "\" already exists");
}

const Message &TodoStore::append(int64_t roomId, const std::string &author,
                                 const std::string &text, const std::string &kind, int64_t todoId,
                                 const std::vector<Attachment> &attachments)
{
    const int64_t now = nowSeconds();
    if (!author.empty())
        if (Room *r = findRoomById(roomId))
            r->members[author] = now;

    // Prune the room's oldest message once it's over the limit.
    const auto inRoom = [roomId](const Message &m) { return m.roomId == roomId; };
    if (static_cast<size_t>(std::count_if(messages_.begin(), messages_.end(), inRoom)) >=
        kMaxMessagesPerRoom)
        pruneAt(std::find_if(messages_.begin(), messages_.end(), inRoom), now);

    messages_.push_back({freeMessageId(messages_, roomId), nextSeq_++, roomId, author, text, now,
                         kind, todoId, 0, attachments});
    if (listener_)
        listener_(roomId);
    return messages_.back();
}

void TodoStore::event(const Todo &t, const std::string &actor, const std::string &what)
{
    if (t.roomId && findRoomById(t.roomId))
        append(t.roomId, actor, what + " " + label(t), "event", t.id);
}

void TodoStore::setMessageListener(std::function<void(int64_t)> fn)
{
    std::lock_guard<std::mutex> lock(mu_);
    listener_ = std::move(fn);
}

std::vector<Room> TodoStore::listRooms(bool includeArchived) const
{
    std::lock_guard<std::mutex> lock(mu_);
    if (includeArchived)
        return rooms_;
    std::vector<Room> out;
    for (const auto &r : rooms_)
        if (!r.archived)
            out.push_back(r);
    return out;
}

Room TodoStore::room(int64_t id) const
{
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &r : rooms_)
        if (r.id == id)
            return r;
    throw StoreError(404, "room not found");
}

Room TodoStore::findRoom(const std::string &ref) const
{
    std::lock_guard<std::mutex> lock(mu_);
    const std::string digits = !ref.empty() && ref[0] == '#' ? ref.substr(1) : ref;
    const bool isId =
        !digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos;
    const auto key = lower(ref);
    for (const auto &r : rooms_)
        if (isId ? std::to_string(r.id) == digits : lower(r.name) == key)
            return r;
    throw StoreError(404, "no room \"" + ref + "\"");
}

Room TodoStore::addRoom(const std::string &name, const std::string &topic)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkRoomName(name, 0);
    Room r{nextRoomId_++, name, topic, nowSeconds()};
    rooms_.push_back(r);
    save();
    return r;
}

Room TodoStore::updateRoom(int64_t id, const std::optional<std::string> &name,
                           const std::optional<std::string> &topic,
                           std::optional<bool> archived)
{
    std::lock_guard<std::mutex> lock(mu_);
    Room *r = findRoomById(id);
    if (!r)
        throw StoreError(404, "room not found");
    if (name)
    {
        checkRoomName(*name, id);
        r->name = *name;
    }
    if (topic)
        r->topic = *topic;
    if (archived && *archived != r->archived)
    {
        const int64_t rid = r->id;
        if (*archived)
        {
            // Move the whole log to the pruned store, which frees the ids.
            const auto inRoom = [rid](const Message &m) { return m.roomId == rid; };
            r->archivedSeqs.clear();
            for (const auto &m : messages_)
                if (inRoom(m))
                    r->archivedSeqs.push_back(m.seq);
            const int64_t now = nowSeconds();
            while (true)
            {
                auto it = std::find_if(messages_.begin(), messages_.end(), inRoom);
                if (it == messages_.end())
                    break;
                pruneAt(it, now);
            }
        }
        else
        {
            // Put back what archiving moved, in time order, with free ids.
            const auto seqs = r->archivedSeqs;
            for (auto seq : seqs)
            {
                auto it = std::find_if(prunedMessages_.begin(), prunedMessages_.end(),
                                       [&](const Message &m) { return m.roomId == rid && m.seq == seq; });
                if (it == prunedMessages_.end())
                    continue;  // purged meanwhile
                Message m = *it;
                prunedMessages_.erase(it);
                m.id = freeMessageId(messages_, rid);
                m.prunedAt = 0;
                insertBySeq(messages_, m);
            }
            r->archivedSeqs.clear();
            ++r->logVersion;
        }
        r->archived = *archived;
    }
    save();
    return *r;
}

Room TodoStore::removeMember(int64_t id, const std::string &name)
{
    std::lock_guard<std::mutex> lock(mu_);
    Room *r = findRoomById(id);
    if (!r)
        throw StoreError(404, "room not found");
    if (!r->members.erase(name))
        throw StoreError(404, "no member named " + name);
    save();
    return *r;
}

void TodoStore::removeRoom(int64_t id)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (!findRoomById(id))
        throw StoreError(404, "room not found");
    for (auto *list : {&todos_, &pruned_})
        for (auto &t : *list)
            if (t.roomId == id)
                t.roomId = 0;
    for (auto *list : {&messages_, &prunedMessages_})
        list->erase(std::remove_if(list->begin(), list->end(),
                                   [id](const Message &m) { return m.roomId == id; }),
                    list->end());
    rooms_.erase(std::remove_if(rooms_.begin(), rooms_.end(),
                                [id](const Room &r) { return r.id == id; }),
                 rooms_.end());
    save();
}

Message TodoStore::post(int64_t roomId, const std::string &author, const std::string &text,
                        const std::vector<Attachment> &attachments)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkRoomExists(roomId);
    if (findRoomById(roomId)->archived)
        throw StoreError(409, "room is archived, unarchive first");
    Message m = append(roomId, author, text, "message", 0, attachments);
    save();
    return m;
}

std::vector<Message> TodoStore::messages(int64_t roomId, int64_t since, size_t limit, int64_t before) const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Message> out;
    for (const auto &m : messages_)
        if (m.roomId == roomId && m.seq > since && (before <= 0 || m.seq < before))
            out.push_back(m);
    if (out.size() > limit)
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
    return out;
}

std::vector<RoomFile> TodoStore::files(int64_t roomId, int64_t before, size_t limit) const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<RoomFile> out;
    // Newest first, a message's files together (so `before` = the last
    // one's seq picks up where this left off).
    for (auto it = messages_.rbegin(); it != messages_.rend() && out.size() < limit; ++it)
    {
        const Message &m = *it;
        if (m.roomId != roomId || (before > 0 && m.seq >= before))
            continue;
        for (const auto &a : m.attachments)
            out.push_back(RoomFile{a, m.id, m.seq, m.author, m.createdAt});
    }
    return out;
}

Json::Value RoomFile::toJson() const
{
    Json::Value v = file.toJson();
    v["messageId"] = static_cast<Json::Int64>(messageId);
    v["seq"] = static_cast<Json::Int64>(seq);
    v["author"] = author.empty() ? Json::Value() : Json::Value(author);
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    return v;
}

int64_t TodoStore::cursor(int64_t roomId) const
{
    std::lock_guard<std::mutex> lock(mu_);
    int64_t last = 0;
    for (const auto &m : messages_)
        if (m.roomId == roomId)
            last = m.seq;
    return last;
}

// ── Pruned messages ──

void TodoStore::pruneAt(std::vector<Message>::iterator it, int64_t now)
{
    Message p = *it;
    messages_.erase(it);
    p.id = freeMessageId(prunedMessages_, p.roomId);
    p.prunedAt = now;
    // Keep at most as many pruned as live: past that, the oldest go for good.
    const auto inRoom = [&p](const Message &m) { return m.roomId == p.roomId; };
    if (static_cast<size_t>(std::count_if(prunedMessages_.begin(), prunedMessages_.end(), inRoom)) >=
        kMaxMessagesPerRoom)
        prunedMessages_.erase(std::find_if(prunedMessages_.begin(), prunedMessages_.end(), inRoom));
    if (Room *r = findRoomById(p.roomId))
        ++r->logVersion;
    insertBySeq(prunedMessages_, std::move(p));
}

Message TodoStore::pruneMessage(int64_t roomId, int64_t id)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkRoomExists(roomId);
    auto it = std::find_if(messages_.begin(), messages_.end(),
                           [&](const Message &m) { return m.roomId == roomId && m.id == id; });
    if (it == messages_.end())
        throw StoreError(404, "message not found");
    const int64_t seq = it->seq;
    pruneAt(it, nowSeconds());
    save();
    return *std::find_if(prunedMessages_.begin(), prunedMessages_.end(),
                         [seq](const Message &m) { return m.seq == seq; });
}

size_t TodoStore::pruneMessages(int64_t roomId, size_t keep)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkRoomExists(roomId);
    const auto inRoom = [roomId](const Message &m) { return m.roomId == roomId; };
    const size_t live = std::count_if(messages_.begin(), messages_.end(), inRoom);
    const size_t n = live > keep ? live - keep : 0;
    const int64_t now = nowSeconds();
    for (size_t i = 0; i < n; ++i)
        pruneAt(std::find_if(messages_.begin(), messages_.end(), inRoom), now);
    if (n)
        save();
    return n;
}

std::vector<Message> TodoStore::listPrunedMessages(int64_t roomId) const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Message> out;
    for (const auto &m : prunedMessages_)
        if (m.roomId == roomId)
            out.push_back(m);
    return out;
}

size_t TodoStore::prunedMessageCount(int64_t roomId) const
{
    std::lock_guard<std::mutex> lock(mu_);
    return std::count_if(prunedMessages_.begin(), prunedMessages_.end(),
                         [roomId](const Message &m) { return m.roomId == roomId; });
}

Message TodoStore::restoreMessage(int64_t roomId, int64_t prunedId)
{
    std::lock_guard<std::mutex> lock(mu_);
    Room *r = findRoomById(roomId);
    if (!r)
        throw StoreError(404, "room not found");
    auto it = std::find_if(prunedMessages_.begin(), prunedMessages_.end(),
                           [&](const Message &m) { return m.roomId == roomId && m.id == prunedId; });
    if (it == prunedMessages_.end())
        throw StoreError(404, "pruned message not found");
    Message m = *it;
    prunedMessages_.erase(it);
    m.id = freeMessageId(messages_, roomId);
    m.prunedAt = 0;
    insertBySeq(messages_, m);  // back in its place by time
    ++r->logVersion;
    save();
    return m;
}

bool TodoStore::removePrunedMessage(int64_t roomId, int64_t prunedId)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(prunedMessages_.begin(), prunedMessages_.end(),
                           [&](const Message &m) { return m.roomId == roomId && m.id == prunedId; });
    if (it == prunedMessages_.end())
        return false;
    prunedMessages_.erase(it);
    save();
    return true;
}

size_t TodoStore::clearPrunedMessages(int64_t roomId)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkRoomExists(roomId);
    const auto before = prunedMessages_.size();
    prunedMessages_.erase(std::remove_if(prunedMessages_.begin(), prunedMessages_.end(),
                                         [roomId](const Message &m) { return m.roomId == roomId; }),
                          prunedMessages_.end());
    const size_t n = before - prunedMessages_.size();
    if (n)
        save();
    return n;
}

void TodoStore::touch(int64_t roomId, const std::string &agent)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (Room *r = findRoomById(roomId); r && !agent.empty())
        r->members[agent] = nowSeconds();
}
