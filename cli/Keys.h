// Keys and key bindings for the interactive UI.
//
// Every action has default keys (actions() below). ~/.config/todo/keys can
// rebind any of them, one per line:
//
//   resize_left = super+left ctrl+left    # replaces the defaults
//   theme = none                          # unbinds it
//
// Keys are written like q, Q, ?, space, enter, esc, tab, shift+tab, up, pgdn,
// del, ctrl+r, alt+x, ctrl+left or super+up. `todo keys` prints them all.
#pragma once

#include "Theme.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

struct Key
{
    enum Type
    {
        None,
        Char,
        Ctrl,  // a control character: Ctrl + the letter in `ch`
        Enter,
        Esc,
        Backspace,
        Tab,
        BackTab,
        Up,
        Down,
        Left,
        Right,
        Home,
        End,
        PgUp,
        PgDn,
        Del,
    } type{None};
    std::string ch;  // for Char: the UTF-8 character; for Ctrl: the letter

    // Modifiers held with it, as terminals report them. Shift is already in
    // the case of a Char, and Ctrl in the Ctrl type.
    enum Mod : uint8_t
    {
        Shift = 1,
        Alt = 2,
        CtrlMod = 4,
        Super = 8,
    };
    uint8_t mods{0};

    bool is(const char *c) const { return type == Char && ch == c && !mods; }
    bool ctrl(char c) const { return type == Ctrl && ch[0] == c; }
};

namespace keys_detail
{
inline const std::vector<std::pair<const char *, Key::Type>> &names()
{
    static const std::vector<std::pair<const char *, Key::Type>> n = {
        {"enter", Key::Enter},   {"return", Key::Enter}, {"esc", Key::Esc},
        {"escape", Key::Esc},    {"backspace", Key::Backspace},
        {"tab", Key::Tab},       {"backtab", Key::BackTab},
        {"up", Key::Up},         {"down", Key::Down},    {"left", Key::Left},
        {"right", Key::Right},   {"home", Key::Home},    {"end", Key::End},
        {"pgup", Key::PgUp},     {"pageup", Key::PgUp},  {"pgdn", Key::PgDn},
        {"pagedown", Key::PgDn}, {"del", Key::Del},      {"delete", Key::Del},
    };
    return n;
}

inline std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// True for a single UTF-8 character.
inline bool oneChar(const std::string &s)
{
    if (s.empty())
        return false;
    const unsigned char b = s[0];
    const size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    return n == s.size();
}
}  // namespace keys_detail

// One key as written in the bindings ("ctrl+left"), ready to match keys.
struct KeySpec
{
    Key::Type type{Key::None};
    std::string ch;
    uint8_t mods{0};  // Alt / CtrlMod / Super / Shift, for non-character keys

    // nullopt if `text` isn't a key we can read.
    static std::optional<KeySpec> parse(const std::string &text)
    {
        using namespace keys_detail;
        KeySpec s;
        std::string rest = text;
        // Modifiers come first: "ctrl+shift+up". A lone "+" is the plus key.
        for (size_t p; rest.size() > 1 && (p = rest.find('+')) != std::string::npos && p > 0;)
        {
            const auto mod = lower(rest.substr(0, p));
            if (mod == "ctrl" || mod == "control" || mod == "c")
                s.mods |= Key::CtrlMod;
            else if (mod == "alt" || mod == "meta" || mod == "m")
                s.mods |= Key::Alt;
            else if (mod == "shift" || mod == "s")
                s.mods |= Key::Shift;
            else if (mod == "super" || mod == "win" || mod == "cmd")
                s.mods |= Key::Super;
            else
                return std::nullopt;
            rest = rest.substr(p + 1);
        }
        const auto name = lower(rest);
        if (name == "space")
            rest = " ", s.type = Key::Char;
        else if (name == "hash")
            rest = "#", s.type = Key::Char;
        else if (name == "plus")
            rest = "+", s.type = Key::Char;
        for (const auto &[n, type] : names())
            if (name == n)
                s.type = type;

        if (s.type == Key::None)
        {
            if (!oneChar(rest))
                return std::nullopt;
            s.type = Key::Char;
        }
        if (s.type == Key::Char)
        {
            s.ch = rest;
            if (s.mods & Key::Shift)  // shift+a is A
            {
                if (s.ch.size() != 1 || !std::isalpha(static_cast<unsigned char>(s.ch[0])))
                    return std::nullopt;
                s.ch[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s.ch[0])));
                s.mods &= ~Key::Shift;
            }
            if (s.mods & Key::CtrlMod)  // terminals only send ctrl + a letter
            {
                if (s.ch.size() != 1 || !std::isalpha(static_cast<unsigned char>(s.ch[0])))
                    return std::nullopt;
                s.type = Key::Ctrl;
                s.ch = lower(s.ch);
                s.mods &= ~Key::CtrlMod;
            }
        }
        if (s.type == Key::Tab && s.mods == Key::Shift)
            s.type = Key::BackTab, s.mods = 0;
        return s;
    }

    bool matches(const Key &k) const
    {
        if (k.type != type)
            return false;
        if (type == Key::Char || type == Key::Ctrl)
            return k.ch == ch && (k.mods & ~Key::Shift) == mods;
        return k.mods == mods;
    }

    // "ctrl+left", "space", "Q": the same words parse() reads.
    std::string name() const
    {
        std::string m;
        if (mods & Key::Super)
            m += "super+";
        if (mods & Key::CtrlMod)
            m += "ctrl+";
        if (mods & Key::Alt)
            m += "alt+";
        if (mods & Key::Shift)
            m += "shift+";
        if (type == Key::Ctrl)
            return m + "ctrl+" + ch;
        if (type == Key::Char)
            return m + (ch == " " ? "space" : ch == "#" ? "hash" : ch);
        if (type == Key::BackTab)
            return m + "shift+tab";
        for (const auto &[n, t] : keys_detail::names())
            if (t == type)
                return m + n;
        return m + "?";
    }

    // For the help screen and hints: arrows as ←, ctrl+x as ^x.
    std::string label() const
    {
        if (type == Key::Ctrl && !mods)
            return "^" + ch;
        auto n = name();
        static const std::vector<std::pair<std::string, std::string>> arrows = {
            {"left", "←"}, {"right", "→"}, {"up", "↑"}, {"down", "↓"}};
        for (const auto &[word, arrow] : arrows)
            if (n.size() >= word.size() && n.compare(n.size() - word.size(), word.size(), word) == 0)
                return n.substr(0, n.size() - word.size()) + arrow;
        return n;
    }
};

struct Action
{
    const char *name;
    const char *keys;   // the defaults, space-separated
    const char *group;  // heading on the help screen
    const char *help;
};

// Every bindable action, in help-screen order.
inline const std::vector<Action> &actions()
{
    static const std::vector<Action> a = {
        {"quit", "q", "General", "quit"},
        {"help", "?", "General", "show the keys"},
        {"new_todo", "n", "General", "add a todo (from anywhere)"},
        {"jump_category", "c", "General", "jump to a category"},
        {"search", "/", "General", "search (a number also matches that id)"},
        {"clear_search", "esc", "General", "clear the search"},
        {"filter_prev", "[", "General", "show all / pending / done / pruned"},
        {"filter_next", "]", "General", "show all / pending / done / pruned"},
        {"hide_done", "H", "General", "hide / show completed"},
        {"prune_done", "C", "General", "prune all done todos (frees their ids)"},
        {"theme", "T", "General", "pick a color theme"},
        {"refresh", "R ctrl+r", "General", "refresh now"},

        {"focus_next", "tab", "Panels", "next panel"},
        {"focus_prev", "shift+tab", "Panels", "previous panel"},
        {"focus_categories", "1 h left", "Panels", "go to categories"},
        {"focus_todos", "2 l right", "Panels", "go to todos"},
        {"focus_details", "3", "Panels", "go to details (to scroll it)"},
        {"resize_left", "ctrl+left", "Panels", "move the side divider left"},
        {"resize_right", "ctrl+right", "Panels", "move the side divider right"},
        {"resize_up", "ctrl+up", "Panels", "make details taller"},
        {"resize_down", "ctrl+down", "Panels", "make details shorter"},
        {"reset_layout", "=", "Panels", "default panel sizes"},

        {"down", "j down", "Moving", "down; past the last todo: into details"},
        {"up", "k up", "Moving", "up; at the top of details: back to todos"},
        {"top", "g home", "Moving", "top"},
        {"bottom", "G end", "Moving", "bottom"},
        {"page_down", "ctrl+d pgdn", "Moving", "half a page down"},
        {"page_up", "ctrl+u pgup", "Moving", "half a page up"},

        {"todo_add", "a", "Todos", "add a todo here"},
        {"todo_toggle", "space enter", "Todos", "done / not done (pruned: restore)"},
        {"todo_edit", "e", "Todos", "edit the title"},
        {"todo_describe", "E", "Todos", "edit the description (in $EDITOR)"},
        {"todo_move", "m", "Todos", "move to a category"},
        {"todo_priority", "p", "Todos", "set priority"},
        {"todo_deadline", "D", "Todos", "set deadline"},
        {"todo_delete", "d x del", "Todos", "delete"},
        {"todo_prune", "P", "Todos", "prune a done todo"},
        {"todo_restore", "u", "Todos", "restore a pruned todo"},
        {"todo_claim", "o", "Todos", "claim / release (working on it)"},
        {"todo_take", "O", "Todos", "take over someone else's claim"},

        {"cat_add", "a", "Categories", "add a subcategory (Rooms: a room)"},
        {"cat_open", "enter", "Categories", "show its todos"},
        {"cat_fold", "space", "Categories", "fold / unfold"},
        {"cat_rename", "e", "Categories", "rename (a category or room)"},
        {"cat_move", "m", "Categories", "move into another category"},
        {"cat_delete", "d x del", "Categories", "delete (a category or room)"},
        {"cat_archive", "A", "Categories", "archive / unarchive"},
        {"cat_style", "s", "Categories", "style (colors...); a room's topic"},

        {"room_say", "i", "Rooms", "post a message to the room"},

        {"confirm_yes", "y Y enter", "Popups", "confirm"},
        {"confirm_no", "n N q esc", "Popups", "cancel"},
        {"pick_choose", "enter space", "Popups", "choose in a list"},
        {"pick_cancel", "q esc", "Popups", "close a list"},
    };
    return a;
}

inline std::string keysPath() { return configDir() + "/keys"; }

class Keymap
{
  public:
    // The defaults, with ~/.config/todo/keys on top; problems go to `warnings`.
    static Keymap load(std::vector<std::string> &warnings)
    {
        Keymap m;
        for (const auto &a : actions())
            m.map_[a.name] = m.parseList(a.keys, std::string("default for ") + a.name, warnings);

        std::ifstream in(keysPath());
        std::string line;
        for (int n = 1; in && std::getline(in, line); ++n)
        {
            const std::string where = "keys:" + std::to_string(n);
            // "#" starts a comment, unless it's a key on its own ("hash" also works).
            for (size_t p = line.find(" #"); p != std::string::npos; p = line.find(" #", p + 1))
                if (p + 2 >= line.size() || line[p + 2] == ' ' || line[p + 2] == '\t')
                {
                    line.resize(p);
                    break;
                }
            line = theme_detail::trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos)
            {
                warnings.push_back(where + ": expected action = key [key...]");
                continue;
            }
            const auto action = keys_detail::lower(theme_detail::trim(line.substr(0, eq)));
            if (!m.map_.count(action))
            {
                warnings.push_back(where + ": unknown action \"" + action + "\" (see: todo keys)");
                continue;
            }
            const auto value = theme_detail::trim(line.substr(eq + 1));
            m.map_[action] = keys_detail::lower(value) == "none"
                                 ? std::vector<KeySpec>{}
                                 : m.parseList(value, where, warnings);
        }
        return m;
    }

    bool is(const Key &k, const char *action) const
    {
        auto it = map_.find(action);
        if (it == map_.end())
            return false;
        for (const auto &s : it->second)
            if (s.matches(k))
                return true;
        return false;
    }

    // "j/↓" for hints; at most `max` keys. "" if it's unbound.
    std::string labels(const char *action, size_t max = 2) const
    {
        std::string out;
        auto it = map_.find(action);
        for (size_t i = 0; it != map_.end() && i < it->second.size() && i < max; ++i)
            out += (i ? "/" : "") + it->second[i].label();
        return out;
    }

    // The whole keymap in the file's own format.
    std::string dump() const
    {
        std::ostringstream out;
        out << "# Key bindings for todo's interactive UI. Copy to " << keysPath() << "\n"
            << "# and keep the lines you change; a line replaces that action's keys.\n"
            << "# Keys: q, Q, ?, space, enter, esc, tab, shift+tab, up, pgdn, del,\n"
            << "# ctrl+r, alt+x, ctrl+left, super+up ... (many terminals and window\n"
            << "# managers keep super+arrow for themselves). \"none\" unbinds an action.\n";
        const char *group = "";
        for (const auto &a : actions())
        {
            if (std::string(group) != a.group)
                out << "\n# " << (group = a.group) << "\n";
            std::string keys;
            for (const auto &s : map_.at(a.name))
                keys += (keys.empty() ? "" : " ") + s.name();
            std::string line = std::string(a.name) + " = " + (keys.empty() ? "none" : keys);
            out << line << std::string(line.size() < 34 ? 34 - line.size() : 1, ' ') << "# "
                << a.help << "\n";
        }
        return out.str();
    }

  private:
    std::vector<KeySpec> parseList(const std::string &text, const std::string &where,
                                   std::vector<std::string> &warnings) const
    {
        std::vector<KeySpec> out;
        std::istringstream in(text);
        for (std::string word; in >> word;)
        {
            if (auto s = KeySpec::parse(word))
                out.push_back(*s);
            else
                warnings.push_back(where + ": can't read key \"" + word + "\"");
        }
        return out;
    }

    std::map<std::string, std::vector<KeySpec>> map_;
};
