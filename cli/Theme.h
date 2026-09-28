#pragma once

// TUI color themes: a few built in, more loaded from
// $XDG_CONFIG_HOME/todo/themes/NAME.theme (default ~/.config/todo/themes).
//
// A theme file is "role = color" lines; '#' starts a comment (after a
// color, leave a space before it). Roles left out keep the default theme's
// color. A color is `default` (the terminal's own), a name (red,
// bright_blue, gray, ...), a 256-color index (0-255) or #rrggbb.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// -1 = terminal default, 0-255 = palette index, kRgb | 0xRRGGBB = truecolor.
constexpr int32_t kDefaultColor = -1;
constexpr int32_t kRgb = 1 << 24;

struct Theme
{
    std::string name{"default"};
    int32_t fg{kDefaultColor};                // plain text
    int32_t bg{kDefaultColor};                // screen background
    int32_t border{kDefaultColor};            // inactive panels
    int32_t borderActive{2};                  // focused panel and popups
    int32_t selectionBg{4};                   // selected row, focused panel
    int32_t selectionBgInactive{8};           // selected row, other panel
    int32_t selectionFg{15};                  // text on a selected row
    int32_t muted{kDefaultColor};             // secondary text (dimmed if default)
    int32_t category{6};                      // category names and tags
    int32_t pending{3};                       // [ ] and "pending"
    int32_t done{2};                          // [x] and "done"
    int32_t accent{6};                        // key hints, labels
    int32_t search{3};                        // search text
    int32_t success{2};                       // status messages, online
    int32_t error{1};                         // errors, offline
};

inline std::string configDir()
{
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string(xdg) + "/todo";
    if (const char *home = std::getenv("HOME"))
        return std::string(home) + "/.config/todo";
    return ".config/todo";
}

inline std::string themesDir() { return configDir() + "/themes"; }

namespace theme_detail
{
inline std::string trim(const std::string &s)
{
    auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
}

inline std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline std::optional<int32_t> parseColor(std::string v)
{
    v = lower(v);
    if (v == "default" || v == "none")
        return kDefaultColor;
    if (v.size() == 7 && v[0] == '#' &&
        v.find_first_not_of("0123456789abcdef", 1) == std::string::npos)
        return kRgb | static_cast<int32_t>(std::stol(v.substr(1), nullptr, 16));
    if (!v.empty() && v.size() <= 3 && v.find_first_not_of("0123456789") == std::string::npos)
    {
        int n = std::stoi(v);
        return n <= 255 ? std::optional<int32_t>(n) : std::nullopt;
    }
    static const char *names[] = {"black", "red", "green", "yellow",
                                  "blue", "magenta", "cyan", "white"};
    for (int i = 0; i < 8; ++i)
    {
        if (v == names[i])
            return i;
        if (v == std::string("bright_") + names[i])
            return i + 8;
    }
    if (v == "gray" || v == "grey")
        return 8;
    return std::nullopt;
}

inline int32_t *role(Theme &t, const std::string &key)
{
    const std::pair<const char *, int32_t Theme::*> roles[] = {
        {"fg", &Theme::fg},
        {"bg", &Theme::bg},
        {"border", &Theme::border},
        {"border_active", &Theme::borderActive},
        {"selection_bg", &Theme::selectionBg},
        {"selection_bg_inactive", &Theme::selectionBgInactive},
        {"selection_fg", &Theme::selectionFg},
        {"muted", &Theme::muted},
        {"category", &Theme::category},
        {"pending", &Theme::pending},
        {"done", &Theme::done},
        {"accent", &Theme::accent},
        {"search", &Theme::search},
        {"success", &Theme::success},
        {"error", &Theme::error},
    };
    for (const auto &[name, member] : roles)
        if (key == name)
            return &(t.*member);
    return nullptr;
}

// Applies "role = color" lines on top of `t`; problems go to `warnings`.
inline void parse(Theme &t, const std::string &text, const std::string &source,
                  std::vector<std::string> &warnings)
{
    std::istringstream in(text);
    std::string line;
    for (int n = 1; std::getline(in, line); ++n)
    {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        const std::string where = source + ":" + std::to_string(n);
        if (eq == std::string::npos)
        {
            warnings.push_back(where + ": expected role = color");
            continue;
        }
        const auto key = lower(trim(line.substr(0, eq)));
        // A color is one word; anything after it is a comment.
        auto val = trim(line.substr(eq + 1));
        val = val.substr(0, val.find_first_of(" \t"));
        int32_t *slot = role(t, key);
        auto color = parseColor(val);
        if (!slot)
            warnings.push_back(where + ": unknown role \"" + key + "\"");
        else if (!color)
            warnings.push_back(where + ": bad color \"" + val + "\"");
        else
            *slot = *color;
    }
}

// Built-in themes, in the same format as theme files.
inline const std::vector<std::pair<std::string, std::string>> &builtins()
{
    static const std::vector<std::pair<std::string, std::string>> b = {
        {"default", ""},
        {"gruvbox", R"(
bg = #282828
fg = #ebdbb2
border = #665c54
border_active = #b8bb26
selection_bg = #458588
selection_bg_inactive = #3c3836
selection_fg = #fbf1c7
muted = #928374
category = #83a598
pending = #fabd2f
done = #b8bb26
accent = #8ec07c
search = #fabd2f
success = #b8bb26
error = #fb4934
)"},
        {"gruvbox-light", R"(
bg = #fbf1c7
fg = #3c3836
border = #bdae93
border_active = #79740e
selection_bg = #d5c4a1
selection_bg_inactive = #ebdbb2
selection_fg = #282828
muted = #928374
category = #076678
pending = #b57614
done = #79740e
accent = #427b58
search = #b57614
success = #79740e
error = #9d0006
)"},
        {"tokyonight", R"(
bg = #1a1b26
fg = #c0caf5
border = #3b4261
border_active = #7aa2f7
selection_bg = #283457
selection_bg_inactive = #292e42
selection_fg = #c0caf5
muted = #565f89
category = #7dcfff
pending = #e0af68
done = #9ece6a
accent = #bb9af7
search = #e0af68
success = #9ece6a
error = #f7768e
)"},
        {"catppuccin", R"(
bg = #1e1e2e
fg = #cdd6f4
border = #45475a
border_active = #cba6f7
selection_bg = #45475a
selection_bg_inactive = #313244
selection_fg = #cdd6f4
muted = #6c7086
category = #89dceb
pending = #f9e2af
done = #a6e3a1
accent = #f5c2e7
search = #f9e2af
success = #a6e3a1
error = #f38ba8
)"},
        {"nord", R"(
bg = #2e3440
fg = #d8dee9
border = #4c566a
border_active = #88c0d0
selection_bg = #434c5e
selection_bg_inactive = #3b4252
selection_fg = #eceff4
muted = #616e88
category = #88c0d0
pending = #ebcb8b
done = #a3be8c
accent = #81a1c1
search = #ebcb8b
success = #a3be8c
error = #bf616a
)"},
    };
    return b;
}
}  // namespace theme_detail

// Built-in names followed by any extra *.theme files, sorted within each.
inline std::vector<std::string> listThemes()
{
    std::vector<std::string> out;
    for (const auto &b : theme_detail::builtins())
        out.push_back(b.first);
    std::set<std::string> user;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(themesDir(), ec))
        if (e.path().extension() == ".theme")
            user.insert(e.path().stem().string());
    for (const auto &u : user)
        if (std::find(out.begin(), out.end(), u) == out.end())
            out.push_back(u);
    return out;
}

// A file in the themes directory wins over a built-in of the same name.
// Returns nullopt if there's no such theme.
inline std::optional<Theme> loadTheme(const std::string &name, std::vector<std::string> &warnings)
{
    Theme t;
    t.name = name;
    const std::string path = themesDir() + "/" + name + ".theme";
    if (std::ifstream in(path); in)
    {
        std::stringstream ss;
        ss << in.rdbuf();
        theme_detail::parse(t, ss.str(), name + ".theme", warnings);
        return t;
    }
    for (const auto &[n, text] : theme_detail::builtins())
        if (n == name)
        {
            theme_detail::parse(t, text, name, warnings);
            return t;
        }
    return std::nullopt;
}

// $TODO_THEME, else the first line of ~/.config/todo/theme, else "default".
inline std::string configuredTheme()
{
    if (const char *env = std::getenv("TODO_THEME"); env && *env)
        return env;
    std::ifstream in(configDir() + "/theme");
    std::string line;
    if (in && std::getline(in, line) && !theme_detail::trim(line).empty())
        return theme_detail::trim(line);
    return "default";
}

inline bool saveThemeChoice(const std::string &name)
{
    std::error_code ec;
    std::filesystem::create_directories(configDir(), ec);
    std::ofstream out(configDir() + "/theme", std::ios::trunc);
    out << name << "\n";
    return static_cast<bool>(out);
}
