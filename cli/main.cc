// todo - command line client for the todo server.
// With no arguments on a terminal it opens the interactive TUI.
#include "Api.h"
#include "Theme.h"
#include "Tui.h"

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
bool gColor = isatty(STDOUT_FILENO);

std::string c(const char *code, const std::string &s)
{
    return gColor ? std::string("\033[") + code + "m" + s + "\033[0m" : s;
}

void usage()
{
    std::cout <<
R"(Usage: todo [-s URL] [command] [args]

With no command, opens the interactive UI (press ? inside for keys).

Todos:
  ui                                  open the interactive UI
  list, ls [all|pending|done] [-c CAT]  show todos, grouped by category
                                      (archived ones only with -c ARCHIVED_CAT)
  add [-c CAT] <title...>             add a todo (to category CAT)
  done <id...>                        mark todo(s) done
  undo <id...>                        mark todo(s) not done
  edit <id> <title...>                rename a todo
  mv <id...> <CAT>                    move todo(s) to CAT ("/" = no category)
  rm <id...>                          delete todo(s)
  clear                               delete all completed todos

Categories (CAT is a path like work/robotics; case doesn't matter):
  cat [ls] [-a]                       show the category tree (-a: and archived)
  cat add <CAT>                       create a category (and missing parents)
  cat rename <CAT> <name>             rename a category
  cat mv <CAT> <PARENT>               move a category ("/" = top level)
  cat rm <CAT>                        delete a category; its todos and
                                      subcategories move to its parent
  cat archive <CAT>                   hide a category, its subcategories and
                                      todos from lists; `clear` keeps them
  cat unarchive <CAT>                 bring an archived category back

Server URL, first match wins: -s/--server URL, $TODO_SERVER, the first line
of ~/.config/todo/server, else http://127.0.0.1:27183

Themes (UI only): press T in the UI to pick one; the choice is saved to
~/.config/todo/theme ($TODO_THEME overrides it). Add your own as
~/.config/todo/themes/NAME.theme, see the README for the format.
)";
}

// Server URL from $TODO_SERVER, then ~/.config/todo/server, then the default.
std::string defaultServer()
{
    if (const char *env = std::getenv("TODO_SERVER"); env && *env)
        return env;
    std::ifstream in(configDir() + "/server");
    std::string line;
    if (in && std::getline(in, line))
    {
        auto b = line.find_first_not_of(" \t\r");
        if (b != std::string::npos)
            return line.substr(b, line.find_last_not_of(" \t\r") - b + 1);
    }
    return "http://127.0.0.1:27183";
}

struct UsageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

std::string joinArgs(const std::vector<std::string> &v, size_t from)
{
    std::string s;
    for (size_t i = from; i < v.size(); ++i)
        s += (i > from ? " " : "") + v[i];
    return s;
}

// Removes "-c VALUE" / "--category VALUE" from args and returns VALUE.
std::optional<std::string> takeCategoryOpt(std::vector<std::string> &args)
{
    for (size_t i = 1; i < args.size(); ++i)
        if ((args[i] == "-c" || args[i] == "--category") && i + 1 < args.size())
        {
            auto v = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
            return v;
        }
    return std::nullopt;
}

int64_t parseId(const std::string &s)
{
    try
    {
        size_t pos;
        auto id = std::stoll(s, &pos);
        if (pos == s.size() && id > 0)
            return id;
    }
    catch (...)
    {
    }
    throw UsageError("invalid id '" + s + "'");
}

int64_t resolveCategory(const CategoryTree &tree, const std::string &path)
{
    auto id = tree.resolve(path);
    if (!id)
        throw UsageError("no category \"" + path + "\" (see `todo cat`)");
    return *id;
}

std::string displayPath(const CategoryTree &tree, int64_t id)
{
    return id ? tree.path(id) : "Inbox";
}

void printTodo(const TodoItem &t, int indent = 0, size_t idWidth = 0)
{
    auto id = std::to_string(t.id);
    if (id.size() < idWidth)
        id = std::string(idWidth - id.size(), ' ') + id;
    std::cout << std::string(indent, ' ') << (t.done ? c("32", "[x]") : c("33", "[ ]"))
              << " " << c("2", id) << "  " << (t.done ? c("2;9", t.title) : t.title) << "\n";
}

// ── todos ──

int cmdList(Api &api, std::vector<std::string> args)
{
    auto catPath = takeCategoryOpt(args);
    const std::string status = args.size() > 1 ? args[1] : "all";
    if (status != "all" && status != "pending" && status != "done")
        throw UsageError("status must be all, pending or done");

    auto tree = api.categories(true);
    const int64_t root = catPath ? resolveCategory(tree, *catPath) : 0;
    // Archived todos only show up when looking inside an archived category.
    const bool archived = tree.inArchive(root);
    std::vector<TodoItem> todos;
    for (auto &t : api.list(status, true))
        if ((!catPath || tree.isWithin(t.categoryId, root) || (root == 0 && t.categoryId == 0)) &&
            (archived || !tree.inArchive(t.categoryId)))
            todos.push_back(t);

    if (todos.empty())
    {
        std::cout << c("2", "No todos.") << "\n";
        return 0;
    }
    size_t idWidth = 1, pending = 0;
    for (const auto &t : todos)
    {
        idWidth = std::max(idWidth, std::to_string(t.id).size());
        pending += !t.done;
    }

    auto printGroup = [&](int64_t catId, int indent) {
        for (const auto &t : todos)
            if (t.categoryId == catId)
                printTodo(t, indent, idWidth);
    };
    auto anyWithin = [&](int64_t catId) {
        for (const auto &t : todos)
            if (tree.isWithin(t.categoryId, catId))
                return true;
        return false;
    };

    if (tree.empty())
        printGroup(0, 0);  // no categories yet: plain list, as before
    else
    {
        int base = 0;
        if (root == 0)
        {
            if (std::any_of(todos.begin(), todos.end(), [](auto &t) { return t.categoryId == 0; }))
            {
                std::cout << c("1", "Inbox") << "\n";
                printGroup(0, 2);
            }
        }
        else
        {
            std::cout << c("1;36", tree.path(root)) << (archived ? c("2", "  (archived)") : "")
                      << "\n";
            printGroup(root, 2);
            base = 1;
        }
        for (const auto &[cat, depth] : tree.flatten(root, !archived))
        {
            if (!anyWithin(cat->id))
                continue;
            const int indent = 2 * (depth + base);
            std::cout << std::string(indent, ' ') << c("1;36", cat->name) << "\n";
            printGroup(cat->id, indent + 2);
        }
    }
    std::cout << c("2", std::to_string(todos.size()) + " shown, " + std::to_string(pending) +
                            " pending")
              << "\n";
    return 0;
}

int cmdAdd(Api &api, std::vector<std::string> args)
{
    auto catPath = takeCategoryOpt(args);
    if (args.size() < 2)
        throw UsageError("usage: todo add [-c CAT] <title...>");
    int64_t catId = 0;
    CategoryTree tree;
    if (catPath)
    {
        tree = api.categories();
        catId = resolveCategory(tree, *catPath);
    }
    auto t = api.add(joinArgs(args, 1), catId);
    std::cout << "Added ";
    printTodo(t);
    if (catId)
        std::cout << c("2", "      in " + tree.path(catId)) << "\n";
    return 0;
}

int cmdMove(Api &api, const std::vector<std::string> &args)
{
    if (args.size() < 3)
        throw UsageError("usage: todo mv <id...> <CAT>   (\"/\" = no category)");
    auto tree = api.categories();
    const int64_t catId = resolveCategory(tree, args.back());
    for (size_t i = 1; i + 1 < args.size(); ++i)
    {
        auto t = api.move(parseId(args[i]), catId);
        std::cout << "Moved #" << t.id << " to " << displayPath(tree, catId) << "\n";
    }
    return 0;
}

// ── categories ──

int cmdCategoryTree(Api &api, bool showArchived)
{
    auto tree = api.categories(true);
    auto todos = api.list("pending", true);
    // Archived todos only count towards archived categories.
    auto openIn = [&](int64_t id, bool exact) {
        const bool archived = tree.inArchive(id);
        size_t n = 0;
        for (const auto &t : todos)
            if (archived || !tree.inArchive(t.categoryId))
                n += exact ? t.categoryId == id : tree.isWithin(t.categoryId, id);
        return n;
    };
    auto line = [&](int indent, const std::string &name, const char *style, size_t open) {
        std::cout << std::string(indent, ' ') << c(style, name);
        if (open)
            std::cout << c("2", "  " + std::to_string(open) + " open");
        std::cout << "\n";
    };

    line(0, "Inbox", "1", openIn(0, true));
    for (const auto &[cat, depth] : tree.flatten(0, true))
        line(2 * depth, cat->name, "1;36", openIn(cat->id, false));
    if (tree.empty())
        std::cout << c("2", "No categories yet. Create one with: todo cat add <name>") << "\n";

    const auto archived = tree.archiveRoots();
    if (archived.empty())
        return 0;
    if (!showArchived)
    {
        std::cout << c("2", std::to_string(archived.size()) +
                                " archived (see them with: todo cat ls -a)")
                  << "\n";
        return 0;
    }
    std::cout << c("1", "Archived") << "\n";
    for (const auto *root : archived)
    {
        line(2, tree.path(root->id), "2;36", openIn(root->id, false));
        for (const auto &[cat, depth] : tree.flatten(root->id))
            line(2 * depth + 4, cat->name, "2;36", openIn(cat->id, false));
    }
    return 0;
}

int cmdCategory(Api &api, const std::vector<std::string> &args)
{
    const std::string sub = args.size() > 1 && args[1][0] != '-' ? args[1] : "ls";
    if (sub == "ls" || sub == "list")
    {
        const std::string opt = args.size() > 2 ? args[2] : args.size() > 1 ? args[1] : "";
        if (!opt.empty() && opt != "ls" && opt != "list" && opt != "-a" && opt != "--archived")
            throw UsageError("usage: todo cat [ls] [-a]");
        return cmdCategoryTree(api, opt == "-a" || opt == "--archived");
    }

    // Archived categories too, so every command can find them by path.
    auto tree = api.categories(true);

    if (sub == "add")
    {
        if (args.size() != 3)
            throw UsageError("usage: todo cat add <CAT>   (e.g. work/robotics)");
        auto parts = CategoryTree::split(args[2]);
        if (parts.empty())
            throw UsageError("category name can't be empty");
        int64_t parent = 0;
        bool created = false;
        for (const auto &name : parts)
        {
            const CategoryItem *existing = nullptr;
            for (const auto *ch : tree.children(parent))
                if (CategoryTree::lower(ch->name) == CategoryTree::lower(name))
                    existing = ch;
            if (existing)
            {
                parent = existing->id;
                continue;
            }
            parent = api.addCategory(name, parent).id;
            created = true;
            tree = api.categories();
        }
        if (!created)
            throw UsageError("category \"" + tree.path(parent) + "\" already exists");
        std::cout << "Created " << c("1;36", tree.path(parent)) << "\n";
        return 0;
    }

    if (sub == "rename")
    {
        if (args.size() < 4)
            throw UsageError("usage: todo cat rename <CAT> <name>");
        const int64_t id = resolveCategory(tree, args[2]);
        if (id == 0)
            throw UsageError("choose a category to rename");
        const auto old = tree.path(id);
        api.renameCategory(id, joinArgs(args, 3));
        std::cout << "Renamed " << old << " to " << c("1;36", api.categories().path(id)) << "\n";
        return 0;
    }

    if (sub == "mv")
    {
        if (args.size() != 4)
            throw UsageError("usage: todo cat mv <CAT> <PARENT>   (\"/\" = top level)");
        const int64_t id = resolveCategory(tree, args[2]);
        const int64_t parent = resolveCategory(tree, args[3]);
        if (id == 0)
            throw UsageError("choose a category to move");
        api.moveCategory(id, parent);
        std::cout << "Moved to " << c("1;36", api.categories().path(id)) << "\n";
        return 0;
    }

    if (sub == "rm")
    {
        if (args.size() != 3)
            throw UsageError("usage: todo cat rm <CAT>");
        const int64_t id = resolveCategory(tree, args[2]);
        if (id == 0)
            throw UsageError("choose a category to delete");
        const int64_t parent = tree.find(id)->parentId;
        api.removeCategory(id);
        std::cout << "Deleted " << tree.path(id) << "; its todos and subcategories moved to "
                  << (parent ? tree.path(parent) : "the top level / Inbox") << "\n";
        return 0;
    }

    if (sub == "archive" || sub == "unarchive")
    {
        const bool archive = sub == "archive";
        if (args.size() != 3)
            throw UsageError("usage: todo cat " + sub + " <CAT>");
        const int64_t id = resolveCategory(tree, args[2]);
        if (id == 0)
            throw UsageError("choose a category to " + sub);
        const auto *cat = tree.find(id);
        if (archive && tree.inArchive(id))
            throw UsageError(tree.path(id) + " is already archived");
        if (!archive && !cat->archived)
        {
            if (!tree.inArchive(id))
                throw UsageError(tree.path(id) + " isn't archived");
            int64_t outer = id;  // the archived ancestor that hides it
            for (const auto *p = cat; p; p = tree.find(p->parentId))
                if (p->archived)
                    outer = p->id;
            throw UsageError(tree.path(id) + " is inside archived " + tree.path(outer) +
                             "; unarchive that instead");
        }
        api.setCategoryArchived(id, archive);
        std::cout << (archive ? "Archived " : "Unarchived ") << c("1;36", tree.path(id)) << "\n";
        return 0;
    }

    throw UsageError("unknown `todo cat` command '" + sub +
                     "' (try: ls, add, rename, mv, rm, archive, unarchive)");
}

int run(Api &api, const std::vector<std::string> &args)
{
    const std::string &cmd = args[0];

    if (cmd == "list" || cmd == "ls")
        return cmdList(api, args);
    if (cmd == "add")
        return cmdAdd(api, args);
    if (cmd == "mv" || cmd == "move")
        return cmdMove(api, args);
    if (cmd == "cat" || cmd == "category" || cmd == "categories")
        return cmdCategory(api, args);

    if (cmd == "done" || cmd == "undo")
    {
        if (args.size() < 2)
            throw UsageError("usage: todo " + cmd + " <id...>");
        for (size_t i = 1; i < args.size(); ++i)
            printTodo(api.setDone(parseId(args[i]), cmd == "done"));
        return 0;
    }

    if (cmd == "edit")
    {
        if (args.size() < 3)
            throw UsageError("usage: todo edit <id> <title...>");
        printTodo(api.rename(parseId(args[1]), joinArgs(args, 2)));
        return 0;
    }

    if (cmd == "rm" || cmd == "delete")
    {
        if (args.size() < 2)
            throw UsageError("usage: todo rm <id...>");
        for (size_t i = 1; i < args.size(); ++i)
        {
            auto id = parseId(args[i]);
            api.remove(id);
            std::cout << "Deleted " << id << "\n";
        }
        return 0;
    }

    if (cmd == "clear")
    {
        std::cout << "Removed " << api.clearDone() << " completed todo(s)\n";
        return 0;
    }

    std::cerr << "Unknown command '" << cmd << "'\n\n";
    usage();
    return 2;
}
}  // namespace

int main(int argc, char *argv[])
{
    std::string server = defaultServer();

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if ((a == "-s" || a == "--server") && i + 1 < argc)
            server = argv[++i];
        else if (a == "-h" || a == "--help")
            return usage(), 0;
        else
            args.push_back(a);
    }

    const bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if ((args.empty() && interactive) || (!args.empty() && args[0] == "ui"))
    {
        if (!interactive)
        {
            std::cerr << c("31", "error: ") << "the UI needs a terminal\n";
            return 1;
        }
        Api api(server, 2.0);
        return runTui(api);
    }
    if (args.empty())
        args.push_back("list");

    try
    {
        Api api(server);
        return run(api, args);
    }
    catch (const std::exception &e)
    {
        std::cerr << c("31", "error: ") << e.what() << "\n";
        return 1;
    }
}
