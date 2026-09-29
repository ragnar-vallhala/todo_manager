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

// Ids are stored as JSON null when 0 ("none").
Json::Value idOrNull(int64_t id)
{
    return id ? Json::Value(static_cast<Json::Int64>(id)) : Json::Value();
}

std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}
}  // namespace

Json::Value Todo::toJson() const
{
    Json::Value v;
    v["id"] = static_cast<Json::Int64>(id);
    v["title"] = title;
    v["done"] = done;
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    v["categoryId"] = idOrNull(categoryId);
    return v;
}

Json::Value Category::toJson() const
{
    Json::Value v;
    v["id"] = static_cast<Json::Int64>(id);
    v["name"] = name;
    v["parentId"] = idOrNull(parentId);
    v["createdAt"] = static_cast<Json::Int64>(createdAt);
    v["archived"] = archived;
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
    if (!todos.isNull() && !todos.isArray())
        throw std::runtime_error("Invalid data file " + path_);

    for (const auto &v : todos)
    {
        Todo t;
        t.id = v["id"].asInt64();
        t.title = v["title"].asString();
        t.done = v["done"].asBool();
        t.createdAt = v["createdAt"].asInt64();
        t.categoryId = v["categoryId"].isNull() ? 0 : v["categoryId"].asInt64();
        nextId_ = std::max(nextId_, t.id + 1);
        todos_.push_back(std::move(t));
    }
    for (const auto &v : cats)
    {
        Category c;
        c.id = v["id"].asInt64();
        c.name = v["name"].asString();
        c.parentId = v["parentId"].isNull() ? 0 : v["parentId"].asInt64();
        c.createdAt = v["createdAt"].asInt64();
        c.archived = v["archived"].asBool();
        nextCategoryId_ = std::max(nextCategoryId_, c.id + 1);
        categories_.push_back(std::move(c));
    }
}

void TodoStore::save() const
{
    Json::Value root;
    root["todos"] = Json::Value(Json::arrayValue);
    root["categories"] = Json::Value(Json::arrayValue);
    for (const auto &t : todos_)
        root["todos"].append(t.toJson());
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

Todo TodoStore::add(const std::string &title, int64_t categoryId)
{
    std::lock_guard<std::mutex> lock(mu_);
    checkCategoryExists(categoryId);
    Todo t{nextId_++, title, false, nowSeconds(), categoryId};
    todos_.push_back(t);
    save();
    return t;
}

std::optional<Todo> TodoStore::update(int64_t id,
                                      const std::optional<std::string> &title,
                                      std::optional<bool> done,
                                      std::optional<int64_t> categoryId)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = std::find_if(todos_.begin(), todos_.end(), [id](const Todo &t) {
        return t.id == id;
    });
    if (it == todos_.end())
        return std::nullopt;
    if (categoryId)
        checkCategoryExists(*categoryId);
    if (title)
        it->title = *title;
    if (done)
        it->done = *done;
    if (categoryId)
        it->categoryId = *categoryId;
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
                                   std::optional<bool> archived)
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
    for (auto &t : todos_)
        if (t.categoryId == id)
            t.categoryId = parent;
    categories_.erase(std::remove_if(categories_.begin(), categories_.end(),
                                     [id](const Category &c) { return c.id == id; }),
                      categories_.end());
    save();
}
