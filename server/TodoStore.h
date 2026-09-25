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

    std::vector<Todo> list() const;
    Todo add(const std::string &title, int64_t categoryId);
    std::optional<Todo> update(int64_t id,
                               const std::optional<std::string> &title,
                               std::optional<bool> done,
                               std::optional<int64_t> categoryId);
    bool remove(int64_t id);
    size_t clearDone();

    std::vector<Category> listCategories() const;
    Category addCategory(const std::string &name, int64_t parentId);
    Category updateCategory(int64_t id,
                            const std::optional<std::string> &name,
                            std::optional<int64_t> parentId);
    // Deletes a category; its todos and subcategories move to its parent.
    void removeCategory(int64_t id);

  private:
    explicit TodoStore(std::string path);
    void load();
    void save() const;  // callers below must hold mu_

    Category *findCategory(int64_t id);
    void checkCategoryExists(int64_t id);
    void checkSiblingName(const std::string &name, int64_t parentId, int64_t selfId);

    mutable std::mutex mu_;
    std::string path_;
    std::vector<Todo> todos_;
    std::vector<Category> categories_;
    int64_t nextId_{1};
    int64_t nextCategoryId_{1};
};
