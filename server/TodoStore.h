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

    Json::Value toJson() const;
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
    Todo add(const std::string &title, int64_t categoryId);
    std::optional<Todo> update(int64_t id,
                               const std::optional<std::string> &title,
                               std::optional<bool> done,
                               std::optional<int64_t> categoryId);
    bool remove(int64_t id);
    size_t clearDone();  // keeps todos in archived categories

    // Without `includeArchived`, archived categories (and everything below
    // them) are left out.
    std::vector<Category> listCategories(bool includeArchived) const;
    Category addCategory(const std::string &name, int64_t parentId);
    Category updateCategory(int64_t id,
                            const std::optional<std::string> &name,
                            std::optional<int64_t> parentId,
                            std::optional<bool> archived);
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
    int64_t nextId_{1};
    int64_t nextCategoryId_{1};
};
