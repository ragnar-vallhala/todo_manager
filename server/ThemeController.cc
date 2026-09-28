#include "ThemeController.h"
#include "HttpUtil.h"
#include "Theme.h"

#include <cstdio>

using http::json;

namespace
{
// A theme color as CSS "#rrggbb"; palette indexes use xterm's colors.
std::string cssColor(int32_t c)
{
    static const int32_t base16[] = {
        0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
        0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff,
    };
    int32_t rgb;
    if (c & kRgb)
        rgb = c & 0xFFFFFF;
    else if (c < 16)
        rgb = base16[c];
    else if (c < 232)
    {
        auto level = [](int v) { return v ? 55 + v * 40 : 0; };
        const int i = c - 16;
        rgb = level(i / 36) << 16 | level(i / 6 % 6) << 8 | level(i % 6);
    }
    else
    {
        const int g = 8 + (c - 232) * 10;
        rgb = g << 16 | g << 8 | g;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06x", rgb);
    return buf;
}
}  // namespace

// GET /api/themes  [{"name", "colors": {role: "#rrggbb"}, "warnings"?: [...]}]
// Only roles a theme sets (to something other than `default`) are listed,
// so the web UI keeps its own look for the rest. Files are re-read on every
// request, so edits show up on the next page load.
void ThemeController::list(const HttpRequestPtr &, Callback &&cb)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &name : listThemes())
    {
        std::vector<std::string> warnings;
        auto theme = loadTheme(name, warnings);
        if (!theme)
            continue;
        Json::Value t;
        t["name"] = name;
        t["colors"] = Json::Value(Json::objectValue);
        for (const auto &[role, member] : theme_detail::roles())
        {
            const int32_t c = (*theme).*member;
            if (theme->assigned.count(role) && c != kDefaultColor)
                t["colors"][role] = cssColor(c);
        }
        for (const auto &w : warnings)
            t["warnings"].append(w);
        arr.append(t);
    }
    cb(json(arr));
}
