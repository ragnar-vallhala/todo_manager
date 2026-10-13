// Category tree helpers shared by the CLI and the TUI.
#pragma once

#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// How a category's name and tags look. Colors are "#rrggbb" or "".
struct CategoryStyle
{
    std::string fg, bg;
    bool bold{false}, italic{false}, underline{false};

    bool empty() const { return fg.empty() && bg.empty() && !bold && !italic && !underline; }

    static CategoryStyle fromJson(const Json::Value &v)
    {
        CategoryStyle s;
        if (!v.isObject())
            return s;
        s.fg = v["fg"].isString() ? v["fg"].asString() : "";
        s.bg = v["bg"].isString() ? v["bg"].asString() : "";
        s.bold = v["bold"].asBool();
        s.italic = v["italic"].asBool();
        s.underline = v["underline"].asBool();
        return s;
    }

    Json::Value toJson() const
    {
        if (empty())
            return Json::Value();
        Json::Value v(Json::objectValue);
        if (!fg.empty())
            v["fg"] = fg;
        if (!bg.empty())
            v["bg"] = bg;
        if (bold)
            v["bold"] = true;
        if (italic)
            v["italic"] = true;
        if (underline)
            v["underline"] = true;
        return v;
    }

    // Named colors offered by all the clients (the web UI has the same list).
    static const std::vector<std::pair<const char *, const char *>> &palette()
    {
        static const std::vector<std::pair<const char *, const char *>> p = {
            {"red", "#e5534b"},  {"orange", "#e08a3c"}, {"yellow", "#c9a227"},
            {"green", "#57a65a"}, {"teal", "#2aa198"},  {"blue", "#4f7fe0"},
            {"purple", "#9a6ce0"}, {"pink", "#d9579b"}, {"brown", "#a0724a"},
            {"gray", "#8a8f98"},
        };
        return p;
    }

    // A palette name or "#rrggbb" to "#rrggbb"; nullopt if it's neither.
    static std::optional<std::string> parseColor(std::string s)
    {
        for (auto &c : s)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        if (s == "grey")
            s = "gray";
        for (const auto &[name, hex] : palette())
            if (s == name)
                return std::string(hex);
        if (s.size() == 7 && s[0] == '#' &&
            s.find_first_not_of("0123456789abcdef", 1) == std::string::npos)
            return s;
        return std::nullopt;
    }

    // "#e5534b" back to "red" when it's in the palette.
    static std::string colorName(const std::string &hex)
    {
        for (const auto &[name, h] : palette())
            if (hex == h)
                return name;
        return hex;
    }

    // "fg=red bg=#202020 bold italic", or "none"; parse() reads this back.
    std::string describe() const
    {
        std::string out;
        auto add = [&](const std::string &s) { out += (out.empty() ? "" : " ") + s; };
        if (!fg.empty())
            add("fg=" + colorName(fg));
        if (!bg.empty())
            add("bg=" + colorName(bg));
        if (bold)
            add("bold");
        if (italic)
            add("italic");
        if (underline)
            add("underline");
        return out.empty() ? "none" : out;
    }

    // Applies words like "fg=red", "bg=none", "bold", "no-italic" or "none"
    // (clear everything) to this style. Returns an error message, or "".
    std::string apply(const std::vector<std::string> &words)
    {
        for (const auto &w : words)
        {
            if (w == "none" || w == "plain")
            {
                *this = CategoryStyle();
                continue;
            }
            const auto eq = w.find('=');
            if (eq != std::string::npos)
            {
                const auto key = w.substr(0, eq), val = w.substr(eq + 1);
                std::string *color = key == "fg" ? &fg : key == "bg" ? &bg : nullptr;
                if (!color)
                    return "unknown setting '" + key + "' (use fg= or bg=)";
                if (val == "none" || val.empty())
                {
                    color->clear();
                    continue;
                }
                auto hex = parseColor(val);
                if (!hex)
                    return "unknown color '" + val + "' (use #rrggbb or one of: " + paletteNames() + ")";
                *color = *hex;
                continue;
            }
            const bool off = w.rfind("no-", 0) == 0;
            const auto flag = off ? w.substr(3) : w;
            bool *b = flag == "bold" ? &bold : flag == "italic" ? &italic
                    : flag == "underline" ? &underline : nullptr;
            if (!b)
                return "unknown style word '" + w + "' (try fg=red bg=blue bold italic underline none)";
            *b = !off;
        }
        return "";
    }

    static std::string paletteNames()
    {
        std::string out;
        for (const auto &[name, hex] : palette())
            out += (out.empty() ? "" : ", ") + std::string(name);
        return out;
    }
};

struct CategoryItem
{
    int64_t id{0};
    std::string name;
    int64_t parentId{0};  // 0 = top level
    bool archived{false};  // set on this category itself, not inherited
    CategoryStyle style;   // set on this category itself

    static CategoryItem fromJson(const Json::Value &v)
    {
        return {v["id"].asInt64(), v["name"].asString(),
                v["parentId"].isNull() ? 0 : v["parentId"].asInt64(),
                v["archived"].asBool(), CategoryStyle::fromJson(v["style"])};
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

    // How `id` looks: its own style, else the nearest ancestor's.
    CategoryStyle styleOf(int64_t id) const
    {
        for (const CategoryItem *c = find(id); c; c = find(c->parentId))
            if (!c->style.empty())
                return c->style;
        return {};
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
