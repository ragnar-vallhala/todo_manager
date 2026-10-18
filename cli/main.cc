// todo - command line client for the todo server.
// With no arguments on a terminal it opens the interactive TUI.
#include "Api.h"
#include "Keys.h"
#include "Theme.h"
#include "Tui.h"
#include "Usage.h"

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
bool gColor = isatty(STDOUT_FILENO);
// Room names by id, for showing where todos are shared (see loadRoomNames).
std::map<int64_t, std::string> gRoomNames;
std::string gAgent;  // who we are in rooms

std::string c(const char *code, const std::string &s)
{
    return gColor ? std::string("\033[") + code + "m" + s + "\033[0m" : s;
}

// SGR codes (without the leading reset) for category `id`'s style, inherited
// from its parents; "" when it has none or output isn't colored. A background
// without a text color gets near-black or near-white text, whichever reads.
std::string catSgr(const CategoryTree &tree, int64_t id)
{
    const auto st = tree.styleOf(id);
    if (!gColor || st.empty())
        return "";
    auto rgb = [](long v, int base) {
        return ";" + std::to_string(base) + ";2;" + std::to_string(v >> 16 & 255) + ";" +
               std::to_string(v >> 8 & 255) + ";" + std::to_string(v & 255);
    };
    auto hex = [](const std::string &h) { return std::stol(h.substr(1), nullptr, 16); };
    std::string code;
    if (st.bold)
        code += ";1";
    if (st.italic)
        code += ";3";
    if (st.underline)
        code += ";4";
    if (!st.fg.empty())
        code += rgb(hex(st.fg), 38);
    if (!st.bg.empty())
    {
        const long v = hex(st.bg);
        if (st.fg.empty())
        {
            const double lum = 0.299 * (v >> 16) + 0.587 * (v >> 8 & 255) + 0.114 * (v & 255);
            code += rgb(lum < 128 ? 0xF4F4F0 : 0x1E2126, 38);
        }
        code += rgb(v, 48);
    }
    return code.substr(1);
}

// A category's name in its style (see `todo cat style`), else in `fallback`.
// With `block`, a background runs to the end of the line.
std::string catName(const CategoryTree &tree, int64_t id, const std::string &text,
                    const char *fallback = "1;36", bool block = false)
{
    const auto st = tree.styleOf(id);
    auto code = catSgr(tree, id);
    if (code.empty())
        return c(fallback, text);
    if (st.fg.empty() && st.bg.empty())
        code += ";36";
    if (st.bg.empty())
        return "\033[0;" + code + "m" + text + "\033[0m";
    return "\033[0;" + code + "m" + (block ? text + "\033[K" : " " + text + " ") + "\033[0m";
}

void usage()
{
    std::cout <<
R"(Usage: todo [-s URL] [--as NAME] [command] [args]

With no command, opens the interactive UI (press ? inside for keys).

Todos:
  ui                                  open the interactive UI
  list, ls [all|pending|done|pruned] [-c CAT] [-r ROOM]
                                      show todos, grouped by category
                                      (archived ones only with -c ARCHIVED_CAT)
  add [-c CAT] [-r ROOM] [-p PRI] [-d DUE] [-m TEXT] [--file PATH...] <title...>
                                      add a todo (to category CAT, shared in
                                      ROOM), -m with a description, --file
                                      with files attached (any type)
  done <id...>                        mark todo(s) done
  undo <id...>                        mark todo(s) not done
  edit <id> <title...>                rename a todo
  set <id...> [-p PRI] [-d DUE] [-r ROOM] [--file PATH...|none]
                                      set or change priority / deadline /
                                      room (none takes it out of its room);
                                      --file attaches files (none: removes
                                      them all)
  desc <id> [TEXT...|-|none]          show a todo's description and
                                      attachments, or set it (- reads it
                                      from stdin, none clears it)
  get <FILE-ID> [-o PATH]             save a todo's attachment (see room get)
  mv <id...> <CAT>                    move todo(s) to CAT ("/" = no category)
  rm <id...>                          delete todo(s); pN deletes a pruned one
  prune [id...]                       move done todo(s) to the pruned list
                                      (all done ones without ids), freeing
                                      their ids; they become p1, p2...
  clear                               same as prune with no ids
  restore <pN...>                     bring pruned todo(s) back, as done
  purge                               delete all pruned todos for good
  claim <id...> [--force]             claim todo(s): you're working on them
                                      (--force takes over someone else's)
  release <id...> [--force]           give up your claim (--force: anyone's)

  PRI is high, medium, low (or h/m/l, 3/2/1), or none to clear it.
  DUE is a date like 2026-10-31, today, tomorrow, +N (days), a weekday
  (mon, friday: the next one), or none to clear it.
  Lists put higher priority first, then the earliest deadline.
  New todos take the lowest free id, so pruning keeps ids small.

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
  cat style <CAT> [WORDS...]          show or change how CAT (and subcategories
                                      without their own) look everywhere:
                                      fg=COLOR bg=COLOR bold italic underline,
                                      no-bold etc., fg=none, or none to reset.
                                      COLOR is #rrggbb or red, orange, yellow,
                                      green, teal, blue, purple, pink, brown, gray

Rooms (where agents and people coordinate; ROOM is a name or id):
  room [ls]                           list rooms
  usage                               Claude plan usage and per-session token
                                      index on every device
  room add <NAME> [TOPIC...]          create a room
  room topic <ROOM> <TEXT...|none>    set what the room is for
  room rename <ROOM> <NAME>           rename a room
  room rm <ROOM>                      delete a room and its messages (its
                                      todos stay, outside any room)
  room show <ROOM> [-n N]             topic, members, todos, last N messages
  room forget <ROOM> <NAME...>        remove members (e.g. old agents) so they
                                      stop being suggested; anyone who posts
                                      or reads the room again rejoins
  room say <ROOM> [--file PATH...] <TEXT...|->
                                      post a message (- reads stdin), with
                                      files attached (any type: images up
                                      to 10 MB, videos 50 MB, others 200 MB;
                                      the text can be left out)
  room get <ROOM> <FILE-ID> [-o PATH] save an attachment (to PATH, a file or
                                      folder, else its name here); prints
                                      where it went
  room files <ROOM> [-n N]            the room's attachments, newest first
                                      (the last N, else all)
  room read <ROOM> [--since ID] [-n N]
                                      messages after ID, else the last N (20)
  room wait <ROOM> [--since ID] [-t SECONDS]
                                      wait for new messages (after ID, else
                                      after the newest) and print them; exits
                                      1 if none came within SECONDS (60)
  room prune <ROOM> [ID...] [--keep N]
                                      move messages out of the log (all but
                                      the newest N, or just these), freeing
                                      their ids; they're kept as p1, p2...
  room pruned <ROOM>                  list a room's pruned messages
  room restore <ROOM> <pN...>         put pruned messages back, in time order
  room purge <ROOM> [pN...]           delete pruned messages for good (all,
                                      or just these)

  Messages print as [#ID] time author: text, in the order they were posted;
  pass the last ID as --since to read on from there. New messages take the
  lowest id free in the room. Changes to a room's todos show up as events.
  Attachments print under the text as [image: NAME WxH, fID],
  [video: NAME WxH M:SS, fID] or [doc] NAME (SIZE, fID); fID is what room get
  takes.

Who you are in rooms, first match wins: --as NAME, $TODO_AGENT, $USER.

Server URL, first match wins: -s/--server URL, $TODO_SERVER, the first line
of ~/.config/todo/server, else http://127.0.0.1:27183

Keys (UI only): `todo keys` prints every action and its keys in the format of
~/.config/todo/keys, where you can rebind them (e.g. resize_left = super+left).

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

// Removes "SHORT VALUE" / "LONG VALUE" from args and returns VALUE.
std::optional<std::string> takeOpt(std::vector<std::string> &args, const char *shortName,
                                   const char *longName)
{
    for (size_t i = 1; i < args.size(); ++i)
        if ((args[i] == shortName || args[i] == longName) && i + 1 < args.size())
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

// A pruned todo's id: "p3" (or just "3").
int64_t parsePrunedId(const std::string &s)
{
    return parseId(!s.empty() && (s[0] == 'p' || s[0] == 'P') ? s.substr(1) : s);
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

// Reads -p / -d from args. Unset options give nullopt; "none" gives 0 / "".
std::pair<std::optional<int>, std::optional<std::string>> takeExtras(std::vector<std::string> &args)
{
    std::pair<std::optional<int>, std::optional<std::string>> out;
    if (auto p = takeOpt(args, "-p", "--priority"))
    {
        out.first = todo::parsePriority(*p);
        if (!out.first)
            throw UsageError("priority must be high, medium, low or none (not '" + *p + "')");
    }
    if (auto d = takeOpt(args, "-d", "--due"))
    {
        out.second = todo::parseDeadline(*d);
        if (!out.second)
            throw UsageError("can't read deadline '" + *d +
                             "' (try 2026-10-31, today, tomorrow, +3, fri or none)");
    }
    return out;
}

// `look` is catSgr() of the todo's category: the line is drawn in it, and a
// background fills the line.
void printTodo(const TodoItem &t, int indent = 0, size_t idWidth = 0, const std::string &look = "",
               bool showRoom = true)
{
    auto id = t.idLabel();
    if (id.size() < idWidth)
        id = std::string(idWidth - id.size(), ' ') + id;
    const std::string base = look.empty() ? "\033[0m" : "\033[0;" + look + "m";
    // Like c(), but going back to the line's look afterwards.
    auto part = [&](const char *code, const std::string &s) {
        if (!gColor)
            return s;
        return "\033[0" + (look.empty() ? "" : ";" + look) + ";" + code + "m" + s + base;
    };
    std::cout << std::string(indent, ' ') << (gColor && !look.empty() ? base : "")
              << (t.pruned() ? part("2", "[-]") : t.done ? part("32", "[x]") : part("33", "[ ]"))
              << " " << part("2", id) << "  ";
    if (t.priority)
        std::cout << part(t.done ? "2" : t.priority == 3 ? "1;31" : t.priority == 2 ? "33" : "36",
                          todo::priorityMarker(t.priority))
                  << " ";
    std::cout << (t.done ? part("2;9", t.title) : t.title);
    if (!t.description.empty())
        std::cout << " " << part("2", "≡");  // has a description: todo desc ID
    if (!t.attachments.empty())  // listed by todo desc ID
        std::cout << " " << part("2", "[" + std::to_string(t.attachments.size()) +
                                          (t.attachments.size() == 1 ? " file]" : " files]"));
    if (!t.deadline.empty())
        std::cout << "  " << part(t.done ? "2" : todo::overdue(t) ? "1;31" : todo::dueToday(t) ? "33" : "2",
                                  "due " + todo::dueLabel(t.deadline));
    if (!t.claimedBy.empty())
        std::cout << "  " << part(t.done ? "2" : t.claimedBy == gAgent ? "1;32" : "35", "@" + t.claimedBy);
    if (t.roomId && showRoom)
    {
        auto it = gRoomNames.find(t.roomId);
        std::cout << "  " << part("2", "room:" + (it != gRoomNames.end() ? it->second
                                                                     : std::to_string(t.roomId)));
    }
    std::cout << (gColor && !look.empty() ? "\033[K\033[0m" : "") << "\n";
}

// Fills gRoomNames; servers without rooms just have none.
void loadRoomNames(Api &api)
{
    try
    {
        for (const auto &r : api.rooms())
            gRoomNames[r.id] = r.name;
    }
    catch (const ApiError &e)
    {
        if (e.offline)
            throw;
    }
}

// A room by name (case-insensitive) or id ("3", "#3").
RoomItem resolveRoom(Api &api, const std::string &ref)
{
    const std::string digits = !ref.empty() && ref[0] == '#' ? ref.substr(1) : ref;
    const bool isId = !digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos;
    for (const auto &r : api.rooms())
        if (isId ? std::to_string(r.id) == digits : CategoryTree::lower(r.name) == CategoryTree::lower(ref))
            return r;
    throw UsageError("no room \"" + ref + "\" (see `todo room`)");
}

// The cursor to read on after message `ref` ("12", "#12") of the room, or 0
// for the start. Ids aren't in time order (pruning frees them), so it's that
// message's seq.
int64_t cursorAfter(Api &api, const RoomItem &room, const std::string &ref)
{
    const std::string digits = !ref.empty() && ref[0] == '#' ? ref.substr(1) : ref;
    if (digits == "0")
        return 0;
    const auto id = parseId(digits);
    for (const auto &m : api.messages(room.id, 0, 1000))
        if (m.id == id)
            return m.seq;
    throw UsageError("no message #" + std::to_string(id) + " in " + room.name +
                     " (pruned, or too old; see: todo room read " + room.name + ")");
}

// ── attachments ──

// Takes every --file PATH out of args.
std::vector<std::string> takeFiles(std::vector<std::string> &args)
{
    std::vector<std::string> out;
    while (auto p = takeOpt(args, "--file", "--file"))
        out.push_back(*p);
    return out;
}

// Uploads the files at `paths`, after checking them all (so a bad one
// doesn't leave the others half done).
std::vector<AttachmentItem> uploadFiles(Api &api, const std::vector<std::string> &paths)
{
    for (const auto &p : paths)
    {
        std::ifstream in(p, std::ios::binary | std::ios::ate);
        if (!in)
            throw UsageError("can't read " + p);
        if (!attach::isFile(p))
            throw UsageError(p + " isn't a regular file");
        const auto type = attach::sniff(p);
        if (static_cast<int64_t>(in.tellg()) > attach::maxFor(type))
            throw UsageError(p + " is over " + attach::maxLabel(type) + ", the most the server takes");
    }
    std::vector<AttachmentItem> out;
    for (const auto &p : paths)
        out.push_back(api.upload(p));
    return out;
}

// "f1a2b3c4d5e6" (as shown) or "1a2b3c4d5e6": the id the server knows.
std::string fileId(const std::string &ref)
{
    std::string id = CategoryTree::lower(ref);
    if (id.size() == 13 && id[0] == 'f')
        id = id.substr(1);
    if (id.size() != 12 || id.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw UsageError("invalid file id '" + ref + "' (they look like f1a2b3c4d5e6)");
    return id;
}

// Downloads `a` to `out` (a file, or a folder to put it in), else to its
// name in the current folder, numbered if that's taken. Prints the path.
void saveAttachment(Api &api, const AttachmentItem &a, const std::optional<std::string> &out)
{
    namespace fs = std::filesystem;
    const std::string name = attach::baseName(a.name).empty() ? a.label() : attach::baseName(a.name);
    fs::path path = out ? fs::path(*out) : fs::path(name);
    std::error_code ec;
    if (out && fs::is_directory(path, ec))
        path /= name;
    else if (!out)
        for (int n = 1; fs::exists(path, ec); ++n)
            path = fs::path(name).stem().string() + "-" + std::to_string(n) +
                   fs::path(name).extension().string();
    const std::string data = api.download(a.id);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f)
        throw UsageError("can't write " + path.string());
    std::cout << path.string() << "\n";
}

// [#12] 14:03 alice: text  (events: [#13] 14:05 alice claimed #3 ...; pruned: [p4])
// Attachments follow the text, one per line: [image: shot.png 800x600, f1a2...].
void printMessage(const MessageItem &m, int indent = 0)
{
    const std::string label = (m.prunedAt ? "[p" : "[#") + std::to_string(m.id) + "] ";
    std::cout << std::string(indent, ' ') << c("2", label + rooms::clock(m.createdAt)) << " ";
    const std::string who = m.author.empty() ? "server" : m.author;
    if (m.event)
    {
        std::cout << c("2;3", who + " " + m.text) << "\n";
        return;
    }
    std::cout << c(m.author == gAgent ? "1;32" : "1;35", who) << ": ";
    // Later lines line up under the first.
    std::istringstream lines(m.text);
    bool first = true;
    for (std::string line; std::getline(lines, line); first = false)
    {
        std::cout << (first ? "" : std::string(indent + 4, ' '));
        // Mentions stand out; ones of us most.
        for (const auto &[part, kind] : rooms::splitMentions(line, gAgent))
            std::cout << (kind == rooms::Mention::Me      ? c("1;30;43", part)
                          : kind == rooms::Mention::Other ? c("1;36", part)
                                                          : part);
        std::cout << "\n";
    }
    for (const auto &a : m.attachments)
    {
        std::cout << (first ? "" : std::string(indent + 4, ' ')) << c("36", a.marker()) << "\n";
        first = false;
    }
}

// ── todos ──

int cmdList(Api &api, std::vector<std::string> args)
{
    auto catPath = takeOpt(args, "-c", "--category");
    auto roomRef = takeOpt(args, "-r", "--room");
    const std::string status = args.size() > 1 ? args[1] : "all";
    if (status != "all" && status != "pending" && status != "done" && status != "pruned")
        throw UsageError("status must be all, pending, done or pruned");

    auto tree = api.categories(true);
    const int64_t root = catPath ? resolveCategory(tree, *catPath) : 0;
    // Archived todos only show up when looking inside an archived category.
    const bool archived = tree.inArchive(root);
    const int64_t roomId = roomRef ? resolveRoom(api, *roomRef).id : 0;
    std::vector<TodoItem> todos;
    for (auto &t : status == "pruned" ? api.listPruned() : api.list(status, true))
        if ((!catPath || tree.isWithin(t.categoryId, root) || (root == 0 && t.categoryId == 0)) &&
            (archived || !tree.inArchive(t.categoryId)) && (!roomId || t.roomId == roomId))
            todos.push_back(t);

    if (todos.empty())
    {
        std::cout << c("2", status == "pruned" ? "Nothing pruned." : "No todos.") << "\n";
        return 0;
    }
    std::stable_sort(todos.begin(), todos.end(), todo::listOrder);
    size_t idWidth = 1, pending = 0;
    for (const auto &t : todos)
    {
        idWidth = std::max(idWidth, t.idLabel().size());
        pending += !t.done;
    }

    auto printGroup = [&](int64_t catId, int indent) {
        for (const auto &t : todos)
            if (t.categoryId == catId)
                printTodo(t, indent, idWidth, catSgr(tree, catId));
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
            std::cout << catName(tree, root, tree.path(root), "1;36", true) << (archived ? c("2", "  (archived)") : "")
                      << "\n";
            printGroup(root, 2);
            base = 1;
        }
        for (const auto &[cat, depth] : tree.flatten(root, !archived))
        {
            if (!anyWithin(cat->id))
                continue;
            const int indent = 2 * (depth + base);
            std::cout << std::string(indent, ' ') << catName(tree, cat->id, cat->name, "1;36", true)
                      << "\n";
            printGroup(cat->id, indent + 2);
        }
    }
    if (status == "pruned")
    {
        std::cout << c("2", std::to_string(todos.size()) +
                                " pruned (todo restore pN brings one back, todo purge deletes them)")
                  << "\n";
        return 0;
    }
    std::cout << c("2", std::to_string(todos.size()) + " shown, " + std::to_string(pending) +
                            " pending")
              << "\n";
    return 0;
}

int cmdAdd(Api &api, std::vector<std::string> args)
{
    auto catPath = takeOpt(args, "-c", "--category");
    auto roomRef = takeOpt(args, "-r", "--room");
    auto [priority, deadline] = takeExtras(args);
    auto description = takeOpt(args, "-m", "--desc");
    const auto paths = takeFiles(args);
    if (args.size() < 2)
        throw UsageError("usage: todo add [-c CAT] [-r ROOM] [-p PRI] [-d DUE] [-m TEXT] "
                         "[--file PATH...] <title...>");
    const int64_t roomId = roomRef ? resolveRoom(api, *roomRef).id : 0;
    int64_t catId = 0;
    CategoryTree tree;
    if (catPath)
    {
        tree = api.categories();
        catId = resolveCategory(tree, *catPath);
    }
    const auto files = uploadFiles(api, paths);
    auto t = api.add(joinArgs(args, 1), catId, priority.value_or(0), deadline.value_or(""),
                     description.value_or(""), roomId);
    if (!files.empty())
        t = api.setAttachments(t.id, files);
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
    auto line = [&](int indent, int64_t id, const std::string &name, const char *style, size_t open) {
        std::cout << std::string(indent, ' ') << (id ? catName(tree, id, name, style) : c(style, name));
        if (open)
            std::cout << c("2", "  " + std::to_string(open) + " open");
        std::cout << "\n";
    };

    line(0, 0, "Inbox", "1", openIn(0, true));
    for (const auto &[cat, depth] : tree.flatten(0, true))
        line(2 * depth, cat->id, cat->name, "1;36", openIn(cat->id, false));
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
        line(2, root->id, tree.path(root->id), "2;36", openIn(root->id, false));
        for (const auto &[cat, depth] : tree.flatten(root->id))
            line(2 * depth + 4, cat->id, cat->name, "2;36", openIn(cat->id, false));
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

    if (sub == "style")
    {
        if (args.size() < 3)
            throw UsageError("usage: todo cat style <CAT> [fg=COLOR] [bg=COLOR] [bold] [italic] "
                             "[underline] [none]");
        const int64_t id = resolveCategory(tree, args[2]);
        if (id == 0)
            throw UsageError("choose a category");
        const auto *cat = tree.find(id);
        if (args.size() == 3)
        {
            std::cout << catName(tree, id, tree.path(id)) << ": ";
            if (!cat->style.empty())
                std::cout << cat->style.describe() << "\n";
            else if (!tree.styleOf(id).empty())
                std::cout << tree.styleOf(id).describe() << " (from its parent)\n";
            else
                std::cout << "none\n";
            return 0;
        }
        CategoryStyle style = cat->style;
        if (auto err = style.apply({args.begin() + 3, args.end()}); !err.empty())
            throw UsageError(err);
        api.setCategoryStyle(id, style);
        tree = api.categories(true);
        std::cout << catName(tree, id, tree.path(id)) << ": " << style.describe() << "\n";
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
                     "' (try: ls, add, rename, mv, rm, archive, unarchive, style)");
}

// ── rooms ──

int cmdRoom(Api &api, std::vector<std::string> args)
{
    const std::string sub = args.size() > 1 ? args[1] : "ls";
    auto need = [&](size_t n, const char *usage) {
        if (args.size() < n)
            throw UsageError(std::string("usage: todo room ") + usage);
    };

    if (sub == "ls" || sub == "list")
    {
        auto list = api.rooms();
        if (list.empty())
        {
            std::cout << c("2", "No rooms yet. Create one with: todo room add <name> [topic]") << "\n";
            return 0;
        }
        const auto todos = api.list("pending", true);
        size_t nameWidth = 0;
        for (const auto &r : list)
            nameWidth = std::max(nameWidth, r.name.size());
        for (const auto &r : list)
        {
            const auto open = std::count_if(todos.begin(), todos.end(),
                                            [&](const TodoItem &t) { return t.roomId == r.id; });
            std::cout << c("1;36", r.name) << std::string(nameWidth - r.name.size() + 2, ' ');
            if (!r.topic.empty())
                std::cout << r.topic << "  ";
            std::string meta = std::to_string(open) + " open";
            if (!r.members.empty())
            {
                meta += " · ";
                for (size_t i = 0; i < r.members.size(); ++i)
                    meta += (i ? ", " : "") + r.members[i].first;
            }
            if (r.prunedMessages)
                meta += " · " + std::to_string(r.prunedMessages) + " pruned";
            std::cout << c("2", meta) << "\n";
        }
        return 0;
    }

    if (sub == "add")
    {
        need(3, "add <NAME> [TOPIC...]");
        auto r = api.addRoom(args[2], joinArgs(args, 3));
        std::cout << "Created room " << c("1;36", r.name) << c("2", "  (#" + std::to_string(r.id) + ")")
                  << "\n";
        return 0;
    }

    if (sub == "topic")
    {
        need(4, "topic <ROOM> <TEXT...|none>");
        const auto room = resolveRoom(api, args[2]);
        std::string topic = joinArgs(args, 3);
        if (topic == "none")
            topic.clear();
        api.updateRoom(room.id, std::nullopt, topic);
        std::cout << c("1;36", room.name) << ": " << (topic.empty() ? c("2", "no topic") : topic) << "\n";
        return 0;
    }

    if (sub == "rename")
    {
        need(4, "rename <ROOM> <NAME>");
        const auto room = resolveRoom(api, args[2]);
        auto r = api.updateRoom(room.id, joinArgs(args, 3), std::nullopt);
        std::cout << "Renamed " << room.name << " to " << c("1;36", r.name) << "\n";
        return 0;
    }

    if (sub == "rm" || sub == "delete")
    {
        need(3, "rm <ROOM>");
        const auto room = resolveRoom(api, args[2]);
        api.removeRoom(room.id);
        std::cout << "Deleted room " << room.name << " and its messages; its todos stay\n";
        return 0;
    }

    if (sub == "forget")
    {
        need(4, "forget <ROOM> <NAME...>");
        const auto room = resolveRoom(api, args[2]);
        int status = 0;
        for (size_t i = 3; i < args.size(); ++i)
        {
            try
            {
                api.removeMember(room.id, args[i]);
                std::cout << "Removed " << c("35", args[i]) << " from " << c("1;36", room.name) << "\n";
            }
            catch (const ApiError &e)
            {
                if (e.offline)
                    throw;
                std::cerr << args[i] << ": " << e.what() << "\n";
                status = 1;
            }
        }
        return status;
    }

    if (sub == "show")
    {
        auto n = takeOpt(args, "-n", "--lines");
        need(3, "show <ROOM> [-n N]");
        const auto room = resolveRoom(api, args[2]);
        const size_t count = n ? static_cast<size_t>(parseId(*n)) : 20;

        std::cout << c("1;36", room.name) << c("2", "  (#" + std::to_string(room.id) + ")") << "\n";
        if (!room.topic.empty())
            std::cout << room.topic << "\n";
        if (!room.members.empty())
        {
            std::cout << c("1", "\nMembers") << "\n";
            for (const auto &[name, seen] : room.members)
                std::cout << "  " << c(name == gAgent ? "1;32" : "35", name) << c("2", "  " + rooms::ago(seen))
                          << "\n";
        }

        std::vector<TodoItem> todos;
        for (const auto &t : api.list("all", true))
            if (t.roomId == room.id)
                todos.push_back(t);
        std::stable_sort(todos.begin(), todos.end(), todo::listOrder);
        std::cout << c("1", "\nTodos") << "\n";
        if (todos.empty())
            std::cout << c("2", "  none (add one with: todo add -r " + room.name + " <title>)") << "\n";
        size_t idWidth = 1;
        for (const auto &t : todos)
            idWidth = std::max(idWidth, t.idLabel().size());
        for (const auto &t : todos)
            printTodo(t, 2, idWidth, "", false);

        std::cout << c("1", "\nMessages") << "\n";
        auto msgs = api.messages(room.id, 0, count);
        if (msgs.empty())
            std::cout << c("2", "  none yet (todo room say " + room.name + " <text>)") << "\n";
        for (const auto &m : msgs)
            printMessage(m, 2);
        return 0;
    }

    if (sub == "say" || sub == "post")
    {
        const auto paths = takeFiles(args);
        need(paths.empty() ? 4 : 3, "say <ROOM> [--file PATH...] <TEXT...|->");
        const auto room = resolveRoom(api, args[2]);
        std::string text = joinArgs(args, 3);
        if (text == "-")
        {
            std::ostringstream in;
            in << std::cin.rdbuf();
            text = in.str();
        }
        printMessage(api.post(room.id, text, uploadFiles(api, paths)));
        return 0;
    }

    if (sub == "get")
    {
        auto out = takeOpt(args, "-o", "--output");
        need(4, "get <ROOM> <FILE-ID> [-o PATH]");
        const auto room = resolveRoom(api, args[2]);
        const auto id = fileId(args[3]);
        // The whole log, a page at a time, then the pruned messages.
        for (int64_t before = 0;;)
        {
            const auto page = api.roomFiles(room.id, before, 1000);
            for (const auto &f : page)
                if (f.file.id == id)
                    return saveAttachment(api, f.file, out), 0;
            if (page.size() < 1000)
                break;
            before = page.back().seq;
        }
        for (const auto &m : api.prunedMessages(room.id))
            for (const auto &a : m.attachments)
                if (a.id == id)
                    return saveAttachment(api, a, out), 0;
        throw UsageError("no file f" + id + " in " + room.name + " (see: todo room read " + room.name + ")");
    }

    if (sub == "files")
    {
        auto n = takeOpt(args, "-n", "--lines");
        need(3, "files <ROOM> [-n N]");
        const auto room = resolveRoom(api, args[2]);
        const size_t want = n ? static_cast<size_t>(parseId(*n)) : SIZE_MAX;
        size_t shown = 0;
        for (int64_t before = 0; shown < want;)
        {
            const auto page = api.roomFiles(room.id, before, 1000);
            for (const auto &f : page)
            {
                if (shown++ >= want)
                    break;
                std::cout << c("2", "[#" + std::to_string(f.messageId) + "] " + rooms::clock(f.createdAt) + " ") +
                                 (f.author.empty() ? "server" : f.author) + "  " + c("36", f.file.marker()) + "\n";
            }
            if (page.size() < 1000)
                break;
            before = page.back().seq;
        }
        if (shown == 0)
            std::cout << "No files in " << room.name << "\n";
        return 0;
    }

    if (sub == "read")
    {
        auto since = takeOpt(args, "--since", "--since");
        auto n = takeOpt(args, "-n", "--lines");
        need(3, "read <ROOM> [--since ID] [-n N]");
        const auto room = resolveRoom(api, args[2]);
        const int64_t after = since ? cursorAfter(api, room, *since) : 0;
        const size_t count = n ? static_cast<size_t>(parseId(*n)) : since ? 1000 : 20;
        for (const auto &m : api.messages(room.id, after, count))
            printMessage(m);
        return 0;
    }

    if (sub == "wait")
    {
        auto since = takeOpt(args, "--since", "--since");
        auto t = takeOpt(args, "-t", "--timeout");
        need(3, "wait <ROOM> [--since ID] [-t SECONDS]");
        const auto room = resolveRoom(api, args[2]);
        const int64_t after = since ? cursorAfter(api, room, *since) : room.cursor;
        double left = t ? std::atof(t->c_str()) : 60;
        if (left < 0)
            throw UsageError("the timeout can't be negative");
        // Waits in short stretches: servers drop requests idle for a minute.
        constexpr double kChunk = 50;
        do
        {
            const double chunk = std::min(left, kChunk);
            auto msgs = api.messages(room.id, after, 1000, chunk);
            if (!msgs.empty())
            {
                for (const auto &m : msgs)
                    printMessage(m);
                return 0;
            }
            left -= chunk;
        } while (left > 0);
        return 1;
    }

    if (sub == "prune")
    {
        auto keep = takeOpt(args, "-k", "--keep");
        need(3, "prune <ROOM> [ID...] [--keep N]");
        const auto room = resolveRoom(api, args[2]);
        if (args.size() == 3)
        {
            const auto n = api.pruneMessages(room.id, keep && *keep != "0" ? static_cast<size_t>(parseId(*keep)) : 0);
            std::cout << "Pruned " << n << " message(s) from " << c("1;36", room.name)
                      << (n ? "; their ids are free again (see: todo room pruned " + room.name + ")" : "")
                      << "\n";
            return 0;
        }
        if (keep)
            throw UsageError("--keep is for pruning all messages; leave out the IDs");
        for (size_t i = 3; i < args.size(); ++i)
        {
            const auto id = parseId(!args[i].empty() && args[i][0] == '#' ? args[i].substr(1) : args[i]);
            const auto p = api.pruneMessage(room.id, id);
            std::cout << "Pruned #" << id << " → ";
            printMessage(p);
        }
        return 0;
    }

    if (sub == "pruned")
    {
        need(3, "pruned <ROOM>");
        const auto room = resolveRoom(api, args[2]);
        const auto msgs = api.prunedMessages(room.id);
        if (msgs.empty())
            std::cout << c("2", "Nothing pruned in " + room.name + ".") << "\n";
        for (const auto &m : msgs)
            printMessage(m);
        return 0;
    }

    if (sub == "restore" || sub == "unprune")
    {
        need(4, "restore <ROOM> <pN...>");
        const auto room = resolveRoom(api, args[2]);
        for (size_t i = 3; i < args.size(); ++i)
        {
            const auto id = parsePrunedId(args[i]);
            std::cout << "Restored p" << id << " as ";
            printMessage(api.restoreMessage(room.id, id));
        }
        return 0;
    }

    if (sub == "purge")
    {
        need(3, "purge <ROOM> [pN...]");
        const auto room = resolveRoom(api, args[2]);
        if (args.size() == 3)
        {
            std::cout << "Deleted " << api.clearPrunedMessages(room.id) << " pruned message(s) of "
                      << room.name << " for good\n";
            return 0;
        }
        for (size_t i = 3; i < args.size(); ++i)
        {
            const auto id = parsePrunedId(args[i]);
            api.removePrunedMessage(room.id, id);
            std::cout << "Deleted p" << id << " for good\n";
        }
        return 0;
    }

    throw UsageError("unknown `todo room` command '" + sub +
                     "' (try: ls, add, topic, rename, rm, show, say, get, read, wait, prune, "
                     "pruned, restore, purge)");
}

int cmdUsage(Api &api)
{
    const auto u = api.usage();
    const double now = cuse::nowSec();
    auto sgr = [](cuse::Level l) {
        return l == cuse::Level::Ok ? "32" : l == cuse::Level::Warn ? "33" : l == cuse::Level::Bad ? "31" : "2";
    };
    for (const auto &s : cuse::line(u, now, 3))
        std::cout << c(sgr(s.level), s.text);
    std::cout << "\n";
    if (u["devices"].empty())
        return 0;
    std::cout << "\n";
    const auto rows = cuse::table(u, now);
    const auto lines = cuse::layout(rows);
    for (size_t i = 0; i < rows.size(); ++i)
        std::cout << (rows[i].header ? c("1", lines[i]) : rows[i].cells[0].empty() ? lines[i] : c(sgr(rows[i].level), lines[i])) << "\n";
    std::cout << "\nIndex = input x1 + output x5 + cache-write x1.25 + cache-read x0.1; "
                 "5h window = since the plan window began.\n";
    return 0;
}

int run(Api &api, const std::vector<std::string> &args)
{
    const std::string &cmd = args[0];
    if (cmd == "usage")
        return cmdUsage(api);
    loadRoomNames(api);

    if (cmd == "room" || cmd == "rooms")
        return cmdRoom(api, args);

    if (cmd == "claim" || cmd == "release")
    {
        auto rest = args;
        const auto force = std::find(rest.begin(), rest.end(), "--force");
        const bool forced = force != rest.end();
        if (forced)
            rest.erase(force);
        if (rest.size() < 2)
            throw UsageError("usage: todo " + cmd + " <id...> [--force]");
        if (gAgent.empty())
            throw UsageError("say who you are: --as NAME or $TODO_AGENT");
        for (size_t i = 1; i < rest.size(); ++i)
        {
            const auto id = parseId(rest[i]);
            try
            {
                printTodo(cmd == "claim" ? api.claim(id, forced) : api.release(id, forced));
            }
            catch (const ApiError &e)
            {
                if (e.offline || forced || std::string(e.what()).find("claimed by") == std::string::npos)
                    throw;
                throw ApiError(std::string(e.what()) + " (--force to " +
                                   (cmd == "claim" ? "take it over" : "release it anyway") + ")",
                               false);
            }
        }
        return 0;
    }

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

    if (cmd == "desc" || cmd == "description" || cmd == "note")
    {
        if (args.size() < 2)
            throw UsageError("usage: todo desc <id> [TEXT...|-|none]");
        const auto id = parseId(args[1]);
        if (args.size() == 2)  // show it
        {
            for (const auto &t : api.list("all", true))
                if (t.id == id)
                {
                    printTodo(t);
                    std::cout << (t.description.empty() ? c("2", "  (no description)")
                                                         : t.description)
                              << "\n";
                    for (const auto &a : t.attachments)
                        std::cout << c("36", a.marker()) << "\n";
                    return 0;
                }
            throw UsageError("no todo #" + std::to_string(id));
        }
        std::string text = joinArgs(args, 2);
        if (text == "none")
            text.clear();
        else if (text == "-")
        {
            std::ostringstream in;
            in << std::cin.rdbuf();
            text = in.str();
        }
        auto t = api.setDescription(id, text);
        printTodo(t);
        std::cout << c("2", t.description.empty() ? "  description cleared" : "  description saved")
                  << "\n";
        return 0;
    }

    if (cmd == "set")
    {
        auto rest = args;
        auto [priority, deadline] = takeExtras(rest);
        auto roomRef = takeOpt(rest, "-r", "--room");
        auto paths = takeFiles(rest);
        if (rest.size() < 2 || (!priority && !deadline && !roomRef && paths.empty()))
            throw UsageError("usage: todo set <id...> [-p PRI] [-d DUE] [-r ROOM] [--file PATH...]"
                             "   (\"none\" clears each)");
        std::optional<int64_t> roomId;
        if (roomRef)
            roomId = *roomRef == "none" || *roomRef == "-" ? 0 : resolveRoom(api, *roomRef).id;
        const bool clearFiles = paths.size() == 1 && paths[0] == "none";
        const auto files = clearFiles ? std::vector<AttachmentItem>{} : uploadFiles(api, paths);
        std::map<int64_t, TodoItem> current;
        if (!paths.empty())
            for (const auto &t : api.list("all", true))
                current[t.id] = t;
        for (size_t i = 1; i < rest.size(); ++i)
        {
            const auto id = parseId(rest[i]);
            TodoItem t;
            if (priority || deadline)
                t = api.setExtras(id, priority, deadline);
            if (roomId)
                t = api.setRoom(id, *roomId);
            if (!paths.empty())
            {
                // New files go after the ones it has (the same file only once).
                auto list = clearFiles ? std::vector<AttachmentItem>{} : current[id].attachments;
                for (const auto &f : files)
                    if (std::none_of(list.begin(), list.end(), [&](auto &a) { return a.id == f.id; }))
                        list.push_back(f);
                t = api.setAttachments(id, list);
            }
            printTodo(t);
            for (const auto &a : paths.empty() ? std::vector<AttachmentItem>{} : t.attachments)
                std::cout << "      " << c("36", a.marker()) << "\n";
        }
        return 0;
    }

    if (cmd == "get")
    {
        auto rest = args;
        auto out = takeOpt(rest, "-o", "--output");
        if (rest.size() != 2)
            throw UsageError("usage: todo get <FILE-ID> [-o PATH]   (a room's: todo room get)");
        const auto id = fileId(rest[1]);
        auto todos = api.list("all", true);
        const auto prunedTodos = api.listPruned();
        todos.insert(todos.end(), prunedTodos.begin(), prunedTodos.end());
        for (const auto &t : todos)
            for (const auto &a : t.attachments)
                if (a.id == id)
                    return saveAttachment(api, a, out), 0;
        throw UsageError("no todo has file f" + id + " (a message's? todo room get ROOM f" + id + ")");
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
            if (args[i][0] == 'p' || args[i][0] == 'P')
            {
                const auto id = parsePrunedId(args[i]);
                api.removePruned(id);
                std::cout << "Deleted p" << id << " for good\n";
                continue;
            }
            auto id = parseId(args[i]);
            api.remove(id);
            std::cout << "Deleted " << id << "\n";
        }
        return 0;
    }

    if (cmd == "prune" || cmd == "clear")
    {
        if (args.size() == 1)
        {
            const auto n = api.pruneDone();
            std::cout << "Pruned " << n << " done todo(s)"
                      << (n ? "; their ids are free again (see: todo ls pruned)" : "") << "\n";
            return 0;
        }
        for (size_t i = 1; i < args.size(); ++i)
        {
            const auto id = parseId(args[i]);
            const auto p = api.prune(id);
            std::cout << "Pruned #" << id << " → " << p.idLabel() << "  " << c("2", p.title) << "\n";
        }
        return 0;
    }

    if (cmd == "restore" || cmd == "unprune")
    {
        if (args.size() < 2)
            throw UsageError("usage: todo restore <pN...>   (see: todo ls pruned)");
        for (size_t i = 1; i < args.size(); ++i)
        {
            const auto id = parsePrunedId(args[i]);
            std::cout << "Restored p" << id << " as ";
            printTodo(api.restore(id));
        }
        return 0;
    }

    if (cmd == "purge")
    {
        std::cout << "Deleted " << api.clearPruned() << " pruned todo(s) for good\n";
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
    gAgent = rooms::defaultAgent();

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if ((a == "-s" || a == "--server") && i + 1 < argc)
            server = argv[++i];
        else if (a == "--as" && i + 1 < argc)
            gAgent = argv[++i];
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
        Api api(server, 2.0, gAgent);
        return runTui(api);
    }
    if (args.empty())
        args.push_back("list");
    if (args[0] == "keys")  // needs no server
    {
        std::vector<std::string> warnings;
        auto keys = Keymap::load(warnings);
        for (const auto &w : warnings)
            std::cerr << c("33", "warning: ") << w << "\n";
        std::cout << keys.dump();
        return warnings.empty() ? 0 : 1;
    }

    try
    {
        Api api(server, 5.0, gAgent);
        return run(api, args);
    }
    catch (const std::exception &e)
    {
        std::cerr << c("31", "error: ") << e.what() << "\n";
        return 1;
    }
}
