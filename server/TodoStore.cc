#include "TodoStore.h"

#include <algorithm>
#include <chrono>
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

// The lowest positive id no todo in `todos` has.
int64_t freeId(const std::vector<Todo> &todos)
{
    std::vector<int64_t> ids;
    for (const auto &t : todos)
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
}

void TodoStore::save() const
{
    Json::Value root;
    root["todos"] = Json::Value(Json::arrayValue);
    root["categories"] = Json::Value(Json::arrayValue);
    root["pruned"] = Json::Value(Json::arrayValue);
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

Todo TodoStore::add(const std::string &title, const Todo &init)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkCategoryExists(init.categoryId);
    Todo t{freeId(todos_), title, false, nowSeconds(), init.categoryId, init.priority, init.deadline,
           init.description};
    todos_.push_back(t);
    save();
    return t;
}

std::optional<Todo> TodoStore::update(int64_t id, const TodoPatch &patch)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) {
        return t.id == id;
    });
    if (it == todos_.end())
        return std::nullopt;
    if (patch.categoryId)
        checkCategoryExists(*patch.categoryId);
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
    save();
    return *it;
}

bool TodoStore::remove(int64_t id)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::remove_if(todos_.begin(), todos_.end(), [id](const Todo &t) {
        return t.id == id;
    });
    if (it == todos_.end())
        return false;
    todos_.erase(it, todos_.end());
    save();
    return true;
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
