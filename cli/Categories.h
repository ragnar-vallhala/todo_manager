// Category tree helpers shared by the CLI and the TUI.
#pragma once

#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct CategoryItem
{
    int64_t id{0};
    std::string name;
    int64_t parentId{0};  // 0 = top level
    bool archived{false};  // set on this category itself, not inherited

    static CategoryItem fromJson(const Json::Value &v)
    {
        return {v["id"].asInt64(), v["name"].asString(),
                v["parentId"].isNull() ? 0 : v["parentId"].asInt64(),
                v["archived"].asBool()};
    }
};

class CategoryTree
{
  public:
    CategoryTree() = default;
    explicit CategoryTree(std::vector<CategoryItem> cats) : cats_(std::move(cats))
    {
        std::sort(cats_.begin(), cats_.end(), [](const auto &a, const auto &b) {
            return lower(a.name) < lower(b.name);
        });
    }

    bool empty() const { return cats_.empty(); }

    const CategoryItem *find(int64_t id) const
    {
        for (const auto &c : cats_)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    // Children of `parentId`, sorted by name.
    std::vector<const CategoryItem *> children(int64_t parentId) const
    {
        std::vector<const CategoryItem *> out;
        for (const auto &c : cats_)
            if (c.parentId == parentId)
                out.push_back(&c);
        return out;
    }

    // "Work/Robotics"; "" for 0 or an unknown id.
    std::string path(int64_t id) const
    {
        std::string p;
        for (const CategoryItem *c = find(id); c; c = find(c->parentId))
            p = p.empty() ? c->name : c->name + "/" + p;
        return p;
    }

    // Path of `id` as seen from inside `ancestor` ("Robotics" rather than
    // "Work/Robotics" when looking at Work).
    std::string relativePath(int64_t id, int64_t ancestor) const
    {
        auto full = path(id), base = path(ancestor);
        if (!base.empty() && full.rfind(base + "/", 0) == 0)
            return full.substr(base.size() + 1);
        return full;
    }

    // True if `id` is `ancestor` or somewhere below it.
    bool isWithin(int64_t id, int64_t ancestor) const
    {
        for (const CategoryItem *c = find(id); c; c = find(c->parentId))
            if (c->id == ancestor)
                return true;
        return false;
    }

    // True if `id` or one of its ancestors is archived.
    bool inArchive(int64_t id) const
    {
        for (const CategoryItem *c = find(id); c; c = find(c->parentId))
            if (c->archived)
                return true;
        return false;
    }

    // The outermost archived categories: archived, with no archived ancestor.
    // Sorted by path.
    std::vector<const CategoryItem *> archiveRoots() const
    {
        std::vector<const CategoryItem *> out;
        for (const auto &c : cats_)
            if (c.archived && !inArchive(c.parentId))
                out.push_back(&c);
        std::sort(out.begin(), out.end(), [this](const auto *a, const auto *b) {
            return lower(path(a->id)) < lower(path(b->id));
        });
        return out;
    }

    // Everything below `root` in display order, with depth (0 = direct child).
    // With `skipArchived`, archived categories and their subtrees are left out.
    std::vector<std::pair<const CategoryItem *, int>> flatten(int64_t root = 0,
                                                              bool skipArchived = false) const
    {
        std::vector<std::pair<const CategoryItem *, int>> out;
        walk(root, 0, skipArchived, out);
        return out;
    }

    // Resolves "Work/Robotics" (case-insensitive, leading "/" optional).
    // "/" or "" resolves to 0 (top level / no category).
    std::optional<int64_t> resolve(const std::string &path) const
    {
        int64_t cur = 0;
        for (const auto &seg : split(path))
        {
            const CategoryItem *next = nullptr;
            for (const auto *c : children(cur))
                if (lower(c->name) == lower(seg))
                    next = c;
            if (!next)
                return std::nullopt;
            cur = next->id;
        }
        return cur;
    }

    static std::vector<std::string> split(const std::string &path)
    {
        std::vector<std::string> out;
        size_t start = 0;
        while (start <= path.size())
        {
            auto end = path.find('/', start);
            if (end == std::string::npos)
                end = path.size();
            auto seg = path.substr(start, end - start);
            auto b = seg.find_first_not_of(' ');
            if (b != std::string::npos)
                out.push_back(seg.substr(b, seg.find_last_not_of(' ') - b + 1));
            start = end + 1;
        }
        return out;
    }

    static std::string lower(std::string s)
    {
        for (auto &c : s)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

  private:
    void walk(int64_t parent, int depth, bool skipArchived,
              std::vector<std::pair<const CategoryItem *, int>> &out) const
    {
        for (const auto *c : children(parent))
        {
            if (skipArchived && c->archived)
                continue;
            out.emplace_back(c, depth);
            walk(c->id, depth + 1, skipArchived, out);
        }
    }

    std::vector<CategoryItem> cats_;
};
