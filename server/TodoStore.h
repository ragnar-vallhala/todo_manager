#pragma once

#include <json/json.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Ids are positive; 0 means "none" (a root category / an uncategorized todo).

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

    bool empty() const
    {
        return !title && !done && !categoryId && !priority && !deadline && !description;
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
    // Takes the category, priority, deadline and description from `init`.
    Todo add(const std::string &title, const Todo &init);
    std::optional<Todo> update(int64_t id, const TodoPatch &patch);
    bool remove(int64_t id);
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

    mutable std::mutex mu_;
    std::string path_;
    std::vector<Todo> todos_;
    std::vector<Category> categories_;
    std::vector<Todo> pruned_;
    int64_t nextCategoryId_{1};
};
