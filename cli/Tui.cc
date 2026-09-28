// Lazygit-style terminal UI, drawn with plain ANSI escapes (no ncurses).
#include "Tui.h"
#include "Api.h"
#include "Theme.h"

#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <clocale>
#include <csignal>
#include <ctime>
#include <cwchar>
#include <string>
#include <set>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

// ───────────────────────────── UTF-8 ─────────────────────────────

// Splits a UTF-8 string into code points, each kept as its own byte string.
std::vector<std::string> splitUtf8(const std::string &s)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();)
    {
        unsigned char b = s[i];
        size_t n = b < 0x80           ? 1
                   : (b >> 5) == 0x6  ? 2
                   : (b >> 4) == 0xE  ? 3
                   : (b >> 3) == 0x1E ? 4
                                      : 1;
        n = std::min(n, s.size() - i);
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

char32_t decode(const std::string &g)
{
    unsigned char b = g[0];
    if (g.size() == 1)
        return b;
    char32_t cp = g.size() == 2 ? (b & 0x1F) : g.size() == 3 ? (b & 0x0F) : (b & 0x07);
    for (size_t i = 1; i < g.size(); ++i)
        cp = (cp << 6) | (static_cast<unsigned char>(g[i]) & 0x3F);
    return cp;
}

bool isControl(char32_t cp)
{
    return cp < 0x20 || cp == 0x7F;
}

// Terminal column width of one code point (control chars count as 1: they
// are drawn as spaces).
int charWidth(const std::string &g)
{
    char32_t cp = decode(g);
    if (cp < 0x7F)
        return 1;
    int w = wcwidth(static_cast<wchar_t>(cp));
    return w < 0 ? 1 : w;
}

int textWidth(const std::string &s)
{
    int w = 0;
    for (const auto &g : splitUtf8(s))
        w += charWidth(g);
    return w;
}

// Cuts s to at most `width` columns, ending in "…" if anything was dropped.
std::string truncate(const std::string &s, int width)
{
    if (width <= 0)
        return "";
    if (textWidth(s) <= width)
        return s;
    std::string out;
    int w = 0;
    for (const auto &g : splitUtf8(s))
    {
        int cw = charWidth(g);
        if (w + cw > width - 1)
            break;
        out += g;
        w += cw;
    }
    return out + "…";
}

// Greedy word wrap to `width` columns; breaks inside words only when needed.
std::vector<std::string> wrap(const std::string &s, int width)
{
    std::vector<std::string> lines;
    std::vector<std::string> line;
    int w = 0;
    auto flush = [&](size_t upTo) {
        std::string out;
        for (size_t i = 0; i < upTo; ++i)
            out += line[i];
        lines.push_back(out);
        line.erase(line.begin(), line.begin() + upTo);
        while (!line.empty() && line.front() == " ")
            line.erase(line.begin());
        w = 0;
        for (const auto &g : line)
            w += charWidth(g);
    };
    for (const auto &g : splitUtf8(s))
    {
        line.push_back(g);
        w += charWidth(g);
        if (w > width)
        {
            auto sp = std::find(line.rbegin(), line.rend(), " ");
            size_t cut = sp != line.rend() ? line.rend() - sp : line.size() - 1;
            flush(cut == 0 ? 1 : cut);
        }
    }
    if (!line.empty() || lines.empty())
        flush(line.size());
    return lines;
}

// ─────────────────────────── Drawing ────────────────────────────

constexpr int32_t Default = kDefaultColor;
enum Attr : uint8_t
{
    Bold = 1,
    Dim = 2,
    Strike = 4,
};

struct Style
{
    int32_t fg{Default};  // see Theme.h for the encoding
    int32_t bg{Default};
    uint8_t attrs{0};

    Style() = default;
    Style(int32_t fg, int32_t bg = Default, uint8_t attrs = 0) : fg(fg), bg(bg), attrs(attrs) {}
    bool operator==(const Style &o) const
    {
        return fg == o.fg && bg == o.bg && attrs == o.attrs;
    }
    bool operator!=(const Style &o) const { return !(*this == o); }

    // Default colors are drawn as defFg / defBg (the theme's own fg and bg).
    std::string sgr(int32_t defFg = Default, int32_t defBg = Default) const
    {
        std::string r = "\033[0";
        if (attrs & Bold)
            r += ";1";
        if (attrs & Dim)
            r += ";2";
        if (attrs & Strike)
            r += ";9";
        r += color(fg == Default ? defFg : fg, 30);
        r += color(bg == Default ? defBg : bg, 40);
        return r + "m";
    }

  private:
    static std::string color(int32_t c, int base)
    {
        if (c < 0)
            return "";
        if (c & kRgb)
            return ";" + std::to_string(base + 8) + ";2;" + std::to_string((c >> 16) & 0xFF) +
                   ";" + std::to_string((c >> 8) & 0xFF) + ";" + std::to_string(c & 0xFF);
        if (c < 8)
            return ";" + std::to_string(base + c);
        if (c < 16)
            return ";" + std::to_string(base + 60 + c - 8);
        return ";" + std::to_string(base + 8) + ";5;" + std::to_string(c);
    }
};

// Off-screen grid of cells; rendered to the terminal in one write per frame.
class Canvas
{
  public:
    // Colors that Default in a Style stands for.
    void setDefaults(int32_t fg, int32_t bg)
    {
        defFg_ = fg;
        defBg_ = bg;
    }

    void reset(int w, int h)
    {
        w_ = w;
        h_ = h;
        cells_.assign(static_cast<size_t>(w * h), Cell{});
    }

    // Draws s at (x, y), clipped to columns [x, maxX). Returns the next x.
    int text(int x, int y, const std::string &s, Style st, int maxX = -1)
    {
        if (maxX < 0 || maxX > w_)
            maxX = w_;
        if (y < 0 || y >= h_)
            return x;
        for (const auto &g : splitUtf8(s))
        {
            char32_t cp = decode(g);
            std::string glyph = isControl(cp) ? " " : g;
            int cw = isControl(cp) ? 1 : charWidth(g);
            if (cw == 0)
            {
                if (x > 0 && x <= maxX)
                    at(x - 1, y).glyph += g;  // combining mark
                continue;
            }
            if (x + cw > maxX)
                break;
            if (x >= 0)
            {
                at(x, y) = {glyph, st};
                if (cw == 2)
                    at(x + 1, y) = {"", st};  // right half of a wide char
            }
            x += cw;
        }
        return x;
    }

    void fill(int x, int y, int w, int h, Style st)
    {
        for (int r = std::max(y, 0); r < std::min(y + h, h_); ++r)
            for (int c = std::max(x, 0); c < std::min(x + w, w_); ++c)
                at(c, r) = {" ", st};
    }

    void box(int x, int y, int w, int h, Style st)
    {
        if (w < 2 || h < 2)
            return;
        for (int c = x + 1; c < x + w - 1; ++c)
        {
            text(c, y, "─", st);
            text(c, y + h - 1, "─", st);
        }
        for (int r = y + 1; r < y + h - 1; ++r)
        {
            text(x, r, "│", st);
            text(x + w - 1, r, "│", st);
        }
        text(x, y, "╭", st);
        text(x + w - 1, y, "╮", st);
        text(x, y + h - 1, "╰", st);
        text(x + w - 1, y + h - 1, "╯", st);
    }

    std::string render() const
    {
        std::string out;
        out.reserve(cells_.size() * 4);
        for (int y = 0; y < h_; ++y)
        {
            out += "\033[" + std::to_string(y + 1) + ";1H";
            Style cur;
            out += cur.sgr(defFg_, defBg_);
            bool prevWide = false;
            for (int x = 0; x < w_; ++x)
            {
                const Cell &c = cells_[y * w_ + x];
                if (c.style != cur)
                {
                    cur = c.style;
                    out += cur.sgr(defFg_, defBg_);
                }
                // A wide glyph only survives if its right half is intact.
                if (c.glyph.empty())
                {
                    if (!prevWide)
                        out += ' ';
                    prevWide = false;
                    continue;
                }
                bool wide = charWidth(c.glyph) == 2;
                if (wide && (x + 1 >= w_ || !cells_[y * w_ + x + 1].glyph.empty()))
                {
                    out += ' ';
                    prevWide = false;
                    continue;
                }
                out += c.glyph;
                prevWide = wide;
            }
        }
        return out + "\033[0m";
    }

  private:
    struct Cell
    {
        std::string glyph{" "};
        Style style;
    };
    Cell &at(int x, int y) { return cells_[y * w_ + x]; }

    int w_{0}, h_{0};
    int32_t defFg_{Default}, defBg_{Default};
    std::vector<Cell> cells_;
};

// ─────────────────────────── Terminal ───────────────────────────

struct Key
{
    enum Type
    {
        None,
        Char,
        Ctrl,
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

    bool is(const char *c) const { return type == Char && ch == c; }
    bool ctrl(char c) const { return type == Ctrl && ch[0] == c; }
};

termios gOrigTermios;
volatile sig_atomic_t gRawMode = 0;
volatile sig_atomic_t gResized = 0;

constexpr char kEnterScreen[] = "\033[?1049h\033[?25l\033[?7l";
constexpr char kLeaveScreen[] = "\033[0m\033[?7h\033[?25h\033[?1049l";

void restoreTerminal()
{
    if (!gRawMode)
        return;
    gRawMode = 0;
    [[maybe_unused]] auto n = ::write(STDOUT_FILENO, kLeaveScreen, sizeof(kLeaveScreen) - 1);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &gOrigTermios);
}

void onFatalSignal(int sig)
{
    restoreTerminal();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void onResize(int)
{
    gResized = 1;
}

class Terminal
{
  public:
    Terminal()
    {
        tcgetattr(STDIN_FILENO, &gOrigTermios);
        termios raw = gOrigTermios;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~OPOST;
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        gRawMode = 1;
        write(kEnterScreen);

        for (int sig : {SIGTERM, SIGHUP, SIGINT, SIGQUIT})
            std::signal(sig, onFatalSignal);
        struct sigaction sa{};
        sa.sa_handler = onResize;  // no SA_RESTART: interrupts poll()
        sigaction(SIGWINCH, &sa, nullptr);
    }
    ~Terminal() { restoreTerminal(); }

    void size(int &w, int &h) const
    {
        winsize ws{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        w = ws.ws_col ? ws.ws_col : 80;
        h = ws.ws_row ? ws.ws_row : 24;
    }

    void write(const std::string &s)
    {
        size_t off = 0;
        while (off < s.size())
        {
            ssize_t n = ::write(STDOUT_FILENO, s.data() + off, s.size() - off);
            if (n <= 0)
                return;
            off += static_cast<size_t>(n);
        }
    }

    bool eof() const { return eof_; }

    // Waits up to timeoutMs for a key; returns Key::None on timeout/resize.
    Key readKey(int timeoutMs)
    {
        if (buf_.empty() && !fill(timeoutMs))
            return {};
        return parse();
    }

  private:
    bool fill(int timeoutMs)
    {
        pollfd p{STDIN_FILENO, POLLIN, 0};
        if (poll(&p, 1, timeoutMs) <= 0)
            return false;
        char tmp[512];
        ssize_t n = ::read(STDIN_FILENO, tmp, sizeof(tmp));
        if (n <= 0)
        {
            eof_ = n == 0;
            return false;
        }
        buf_.append(tmp, static_cast<size_t>(n));
        return true;
    }

    Key take(size_t n, Key::Type t)
    {
        Key k{t, buf_.substr(0, n)};
        buf_.erase(0, n);
        return k;
    }

    Key parse()
    {
        unsigned char b = buf_[0];
        if (b == 0x1B)
        {
            if (buf_.size() == 1 && !fill(30))
                return take(1, Key::Esc);
            if (buf_[1] != '[' && buf_[1] != 'O')
                return take(1, Key::Esc);  // Alt+x arrives as Esc, x
            size_t i = 2;
            for (;;)
            {
                while (i < buf_.size() && !(buf_[i] >= 0x40 && buf_[i] <= 0x7E))
                    ++i;
                if (i < buf_.size())
                    break;
                if (!fill(30))
                {
                    buf_.clear();
                    return {Key::Esc};
                }
            }
            int param = std::atoi(buf_.substr(2, i - 2).c_str());
            char fin = buf_[i];
            buf_.erase(0, i + 1);
            switch (fin)
            {
                case 'A': return {Key::Up};
                case 'B': return {Key::Down};
                case 'C': return {Key::Right};
                case 'D': return {Key::Left};
                case 'H': return {Key::Home};
                case 'F': return {Key::End};
                case 'Z': return {Key::BackTab};
                case '~':
                    switch (param)
                    {
                        case 1: case 7: return {Key::Home};
                        case 4: case 8: return {Key::End};
                        case 3: return {Key::Del};
                        case 5: return {Key::PgUp};
                        case 6: return {Key::PgDn};
                    }
            }
            return {};  // unknown sequence: ignore
        }
        if (b == '\r' || b == '\n')
            return take(1, Key::Enter);
        if (b == 0x7F || b == 0x08)
            return take(1, Key::Backspace);
        if (b == '\t')
            return take(1, Key::Tab);
        if (b < 0x20)
        {
            buf_.erase(0, 1);
            return {Key::Ctrl, std::string(1, static_cast<char>('a' + b - 1))};
        }
        size_t n = b < 0x80           ? 1
                   : (b >> 5) == 0x6  ? 2
                   : (b >> 4) == 0xE  ? 3
                   : (b >> 3) == 0x1E ? 4
                                      : 1;
        while (buf_.size() < n && fill(30))
        {
        }
        return take(std::min(n, buf_.size()), Key::Char);
    }

    std::string buf_;
    bool eof_{false};
};

// ────────────────────────── Line editor ─────────────────────────

class LineEdit
{
  public:
    void set(const std::string &s)
    {
        chars_ = splitUtf8(s);
        cur_ = chars_.size();
    }

    std::string text() const
    {
        std::string s;
        for (const auto &g : chars_)
            s += g;
        return s;
    }

    // Returns true if the key was an editing key.
    bool handle(const Key &k)
    {
        switch (k.type)
        {
            case Key::Char: chars_.insert(chars_.begin() + cur_++, k.ch); return true;
            case Key::Backspace:
                if (cur_ > 0)
                    chars_.erase(chars_.begin() + --cur_);
                return true;
            case Key::Del:
                if (cur_ < chars_.size())
                    chars_.erase(chars_.begin() + cur_);
                return true;
            case Key::Left: cur_ -= cur_ > 0; return true;
            case Key::Right: cur_ += cur_ < chars_.size(); return true;
            case Key::Home: cur_ = 0; return true;
            case Key::End: cur_ = chars_.size(); return true;
            case Key::Ctrl:
                switch (k.ch[0])
                {
                    case 'a': cur_ = 0; return true;
                    case 'e': cur_ = chars_.size(); return true;
                    case 'u':
                        chars_.erase(chars_.begin(), chars_.begin() + cur_);
                        cur_ = 0;
                        return true;
                    case 'k': chars_.erase(chars_.begin() + cur_, chars_.end()); return true;
                    case 'w':
                        while (cur_ > 0 && chars_[cur_ - 1] == " ")
                            chars_.erase(chars_.begin() + --cur_);
                        while (cur_ > 0 && chars_[cur_ - 1] != " ")
                            chars_.erase(chars_.begin() + --cur_);
                        return true;
                }
                return false;
            default: return false;
        }
    }

    // Draws into `width` columns at (x, y), scrolled so the cursor is
    // visible. Returns the cursor's screen column.
    int draw(Canvas &c, int x, int y, int width, Style st) const
    {
        size_t start = 0;
        int before = 0;  // columns between start and the cursor
        for (size_t i = 0; i < cur_; ++i)
            before += charWidth(chars_[i]);
        while (start < cur_ && before + 1 > width)
            before -= charWidth(chars_[start++]);
        int cx = x;
        for (size_t i = start; i < chars_.size() && cx < x + width; ++i)
            cx = c.text(cx, y, chars_[i], st, x + width);
        return x + before;
    }

  private:
    std::vector<std::string> chars_;
    size_t cur_{0};
};

// ───────────────────────────── App ──────────────────────────────

enum class Status { All, Pending, Done };
constexpr const char *kStatusNames[] = {"All", "Pending", "Done"};

enum class Mode
{
    Normal,
    AddTodo,
    EditTodo,
    AddCategory,
    RenameCategory,
    Search,
    Pick,
    ConfirmDeleteTodo,
    ConfirmDeleteCategory,
    ConfirmClear,
    Help,
};
enum class Focus { Categories, Todos };
enum class PickFor { ShowCategory, MoveTodo, MoveCategory, Theme };

// Pseudo-categories at the top of the categories panel.
constexpr int64_t kAll = -1;   // every todo
constexpr int64_t kInbox = 0;  // todos without a category

const std::vector<std::pair<const char *, const char *>> kHelp = {
    {"j/k  ↑/↓", "move"},
    {"g/G", "jump to top / bottom"},
    {"tab  h/l  1 2", "switch panel"},
    {"space", "toggle done / fold category"},
    {"a", "add todo / subcategory"},
    {"n", "add todo (from anywhere)"},
    {"e", "edit todo / rename category"},
    {"m", "move to another category"},
    {"d", "delete"},
    {"c", "jump to a category"},
    {"C", "clear all completed"},
    {"/", "search (esc clears)"},
    {"[  ]", "show all / pending / done"},
    {"H", "hide / show completed"},
    {"T", "pick a color theme"},
    {"R", "refresh now"},
    {"q", "quit"},
};

std::string formatTime(int64_t t)
{
    time_t tt = static_cast<time_t>(t);
    tm tmv{};
    localtime_r(&tt, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

std::string lower(std::string s)
{
    for (auto &ch : s)
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    return s;
}

std::string trim(const std::string &s)
{
    auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

struct CatRow
{
    int64_t id;
    std::string name;
    int depth;
    bool hasChildren;
};

struct PickOption
{
    int64_t id;
    std::string label;
    int depth;
};

class App
{
  public:
    explicit App(Api &api) : api_(api) {}

    int run()
    {
        Terminal term;
        setTheme(configuredTheme());
        reload();
        while (!quit_ && !term.eof())
        {
            draw(term);
            Key k = term.readKey(250);
            if (k.type != Key::None)
                handle(k);
            else if (mode_ == Mode::Normal && Clock::now() - lastLoad_ > std::chrono::seconds(2))
                reload();  // pick up changes from the web UI / other clients
        }
        return 0;
    }

  private:
    // ── data ──

    void reload()
    {
        lastLoad_ = Clock::now();
        try
        {
            todos_ = api_.list();
            tree_ = api_.categories();
            online_ = true;
        }
        catch (const ApiError &e)
        {
            if (online_ || !e.offline)
                flash(e.what(), true);
            online_ = !e.offline;
        }
        syncSelection();
    }

    // Runs an API action, reports failures, reloads. Returns true on success.
    template <class F>
    bool act(F &&f)
    {
        bool ok = true;
        try
        {
            f();
        }
        catch (const ApiError &e)
        {
            online_ = !e.offline;
            flash(e.what(), true);
            ok = false;
        }
        reload();
        return ok;
    }

    void flash(const std::string &msg, bool error)
    {
        flash_ = msg;
        flashError_ = error;
        flashUntil_ = Clock::now() + std::chrono::seconds(3);
    }

    // Is todo t shown when category `view` (kAll, kInbox or an id) is selected?
    bool inView(const TodoItem &t, int64_t view) const
    {
        if (view == kAll)
            return true;
        if (view == kInbox)
            return t.categoryId == 0;
        return tree_.isWithin(t.categoryId, view);
    }

    size_t openCount(int64_t view) const
    {
        return std::count_if(todos_.begin(), todos_.end(),
                             [&](const TodoItem &t) { return !t.done && inView(t, view); });
    }

    std::string viewName(int64_t view) const
    {
        return view == kAll ? "All" : view == kInbox ? "Inbox" : tree_.path(view);
    }

    // Where new todos/subcategories go: the selected category, else none.
    int64_t target() const { return catSelId_ > 0 ? catSelId_ : 0; }

    std::vector<CatRow> catRows() const
    {
        std::vector<CatRow> rows{{kAll, "All", 0, false}, {kInbox, "Inbox", 0, false}};
        addCatRows(0, 0, rows);
        return rows;
    }

    void addCatRows(int64_t parent, int depth, std::vector<CatRow> &rows) const
    {
        for (const auto *c : tree_.children(parent))
        {
            const bool kids = !tree_.children(c->id).empty();
            rows.push_back({c->id, c->name, depth, kids});
            if (kids && !collapsed_.count(c->id))
                addCatRows(c->id, depth + 1, rows);
        }
    }

    std::vector<const TodoItem *> visible() const
    {
        std::vector<const TodoItem *> v;
        std::string q = lower(search_.text());
        // A number (optionally "#12") also matches the todo with that id,
        // which is listed first.
        std::string digits = !q.empty() && q[0] == '#' ? q.substr(1) : q;
        const bool isId = !digits.empty() && digits.size() <= 18 &&
                          digits.find_first_not_of("0123456789") == std::string::npos;
        const int64_t qid = isId ? std::stoll(digits) : 0;
        for (const auto &t : todos_)
        {
            if ((status_ == Status::Pending && t.done) || (status_ == Status::Done && !t.done))
                continue;
            if (!inView(t, catSelId_))
                continue;
            const bool idMatch = isId && t.id == qid;
            if (!q.empty() && !idMatch && lower(t.title).find(q) == std::string::npos)
                continue;
            if (idMatch)
                v.insert(v.begin(), &t);
            else
                v.push_back(&t);
        }
        return v;
    }

    const TodoItem *selected() const
    {
        auto v = visible();
        return sel_ < v.size() ? v[sel_] : nullptr;
    }

    // Keeps both selections on the same items across reloads when possible.
    void syncSelection()
    {
        auto rows = catRows();
        auto findRow = [&](int64_t id) -> long {
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].id == id)
                    return static_cast<long>(i);
            return -1;
        };
        long idx = findRow(catSelId_);
        // Hidden by a folded parent, or deleted: fall back to an ancestor.
        for (int64_t id = catSelId_; idx < 0 && id > 0;)
        {
            const auto *c = tree_.find(id);
            id = c ? c->parentId : 0;
            idx = id > 0 ? findRow(id) : -1;
        }
        catSel_ = idx < 0 ? 0 : static_cast<size_t>(idx);
        catSelId_ = rows[catSel_].id;

        auto v = visible();
        if (v.empty())
        {
            sel_ = 0;
            return;
        }
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i]->id == selId_)
                sel_ = i;
        sel_ = std::min(sel_, v.size() - 1);
        selId_ = v[sel_]->id;
    }

    void moveSel(long delta)
    {
        auto v = visible();
        if (v.empty())
            return;
        long s = std::clamp<long>(static_cast<long>(sel_) + delta, 0, static_cast<long>(v.size()) - 1);
        sel_ = static_cast<size_t>(s);
        selId_ = v[sel_]->id;
    }

    void selectCategory(int64_t id)
    {
        // Unfold ancestors so the row is visible.
        for (const auto *c = tree_.find(id); c; c = tree_.find(c->parentId))
            collapsed_.erase(c->parentId);
        catSelId_ = id;
        sel_ = 0;
        selId_ = 0;
        syncSelection();
    }

    void moveCatSel(long delta)
    {
        auto rows = catRows();
        long s = std::clamp<long>(static_cast<long>(catSel_) + delta, 0, static_cast<long>(rows.size()) - 1);
        selectCategory(rows[static_cast<size_t>(s)].id);
    }

    void setStatus(int s)
    {
        status_ = static_cast<Status>((s + 3) % 3);
        syncSelection();
    }

    void startInput(Mode mode, const std::string &text)
    {
        input_.set(text);
        mode_ = mode;
    }

    // ── theme ──

    // Loads a theme by name (re-reading its file); keeps the current one on failure.
    bool setTheme(const std::string &name)
    {
        std::vector<std::string> warnings;
        auto t = loadTheme(name, warnings);
        if (!t)
        {
            flash("No theme \"" + name + "\" (built-in or in " + themesDir() + ")", true);
            return false;
        }
        theme_ = *t;
        if (!warnings.empty())
        {
            auto more = warnings.size() - 1;
            flash(warnings.front() + (more ? " (+" + std::to_string(more) + " more)" : ""), true);
        }
        return true;
    }

    // Secondary text: the theme's muted color, or dimmed text if it has none.
    Style muted(int32_t bg = Default, uint8_t attrs = 0) const
    {
        return theme_.muted == Default ? Style(Default, bg, attrs | Dim)
                                       : Style(theme_.muted, bg, attrs);
    }

    void openThemePicker()
    {
        themeNames_ = listThemes();
        themeBefore_ = theme_.name;
        pickFor_ = PickFor::Theme;
        pickOptions_.clear();
        pickSel_ = 0;
        for (size_t i = 0; i < themeNames_.size(); ++i)
        {
            pickOptions_.push_back({static_cast<int64_t>(i), themeNames_[i], 0});
            if (themeNames_[i] == theme_.name)
                pickSel_ = i;
        }
        pickOffset_ = 0;
        mode_ = Mode::Pick;
    }

    // ── picker ──

    void openPicker(PickFor purpose, int64_t subject)
    {
        pickFor_ = purpose;
        pickSubject_ = subject;
        pickOptions_.clear();
        int64_t current = 0;
        switch (purpose)
        {
            case PickFor::ShowCategory:
                pickOptions_.push_back({kAll, "All", 0});
                pickOptions_.push_back({kInbox, "Inbox", 0});
                current = catSelId_;
                break;
            case PickFor::MoveTodo:
                pickOptions_.push_back({kInbox, "Inbox (no category)", 0});
                for (const auto &t : todos_)
                    if (t.id == subject)
                        current = t.categoryId;
                break;
            case PickFor::MoveCategory:
                pickOptions_.push_back({0, "Top level", 0});
                if (const auto *c = tree_.find(subject))
                    current = c->parentId;
                break;
        }
        for (const auto &[c, depth] : tree_.flatten())
        {
            if (purpose == PickFor::MoveCategory && tree_.isWithin(c->id, subject))
                continue;  // can't move a category inside itself
            pickOptions_.push_back({c->id, c->name, depth});
        }
        pickSel_ = 0;
        for (size_t i = 0; i < pickOptions_.size(); ++i)
            if (pickOptions_[i].id == current)
                pickSel_ = i;
        pickOffset_ = 0;
        mode_ = Mode::Pick;
    }

    void handlePick(const Key &k)
    {
        const long n = static_cast<long>(pickOptions_.size());
        auto move = [&](long d) {
            pickSel_ = static_cast<size_t>(std::clamp<long>(static_cast<long>(pickSel_) + d, 0, n - 1));
        };
        const size_t before = pickSel_;
        if (k.type == Key::Esc || k.is("q"))
        {
            mode_ = Mode::Normal;
            if (pickFor_ == PickFor::Theme)
                setTheme(themeBefore_);
        }
        else if (k.is("j") || k.type == Key::Down)
            move(1);
        else if (k.is("k") || k.type == Key::Up)
            move(-1);
        else if (k.is("g") || k.type == Key::Home)
            move(-n);
        else if (k.is("G") || k.type == Key::End)
            move(n);
        else if (k.type == Key::Enter || k.is(" "))
        {
            const int64_t id = pickOptions_[pickSel_].id;
            const int64_t subject = pickSubject_;
            mode_ = Mode::Normal;
            switch (pickFor_)
            {
                case PickFor::ShowCategory:
                    selectCategory(id);
                    focus_ = Focus::Todos;
                    break;
                case PickFor::MoveTodo:
                    act([&] {
                        api_.move(subject, id);
                        flash("Moved #" + std::to_string(subject) + " to " + viewName(id), false);
                    });
                    break;
                case PickFor::MoveCategory:
                    act([&] {
                        auto c = api_.moveCategory(subject, id);
                        flash("Moved to " + (id ? tree_.path(id) + "/" : "") + c.name, false);
                    });
                    collapsed_.erase(id);
                    selectCategory(subject);
                    break;
                case PickFor::Theme:
                    if (!saveThemeChoice(theme_.name))
                        flash("Couldn't save theme choice to " + configDir() + "/theme", true);
                    else if (std::getenv("TODO_THEME"))
                        flash("Theme " + theme_.name + " saved, but $TODO_THEME overrides it", true);
                    else
                        flash("Theme: " + theme_.name, false);
                    break;
            }
        }
        if (pickFor_ == PickFor::Theme && mode_ == Mode::Pick && pickSel_ != before)
            setTheme(themeNames_[pickSel_]);  // live preview
    }

    // ── input ──

    void handle(const Key &k)
    {
        if (k.ctrl('c'))
        {
            quit_ = true;
            return;
        }
        switch (mode_)
        {
            case Mode::Normal: handleNormal(k); break;
            case Mode::Help: mode_ = Mode::Normal; break;
            case Mode::AddTodo:
            case Mode::EditTodo:
            case Mode::AddCategory:
            case Mode::RenameCategory: handleInput(k); break;
            case Mode::Search: handleSearch(k); break;
            case Mode::Pick: handlePick(k); break;
            case Mode::ConfirmDeleteTodo:
            case Mode::ConfirmDeleteCategory:
            case Mode::ConfirmClear: handleConfirm(k); break;
        }
    }

    void handleNormal(const Key &k)
    {
        if (k.is("q"))
            quit_ = true;
        else if (k.is("?"))
            mode_ = Mode::Help;
        else if (k.type == Key::Tab || k.type == Key::BackTab)
            focus_ = focus_ == Focus::Todos ? Focus::Categories : Focus::Todos;
        else if (k.is("h") || k.type == Key::Left || k.is("1"))
            focus_ = Focus::Categories;
        else if (k.is("l") || k.type == Key::Right || k.is("2"))
            focus_ = Focus::Todos;
        else if (k.is("["))
            setStatus(static_cast<int>(status_) - 1);
        else if (k.is("]"))
            setStatus(static_cast<int>(status_) + 1);
        else if (k.is("H"))
        {
            const bool hide = status_ != Status::Pending;
            setStatus(static_cast<int>(hide ? Status::Pending : Status::All));
            flash(hide ? "Hiding completed" : "Showing completed", false);
        }
        else if (k.is("n"))
            startInput(Mode::AddTodo, "");
        else if (k.is("c"))
            openPicker(PickFor::ShowCategory, 0);
        else if (k.is("C"))
        {
            if (std::any_of(todos_.begin(), todos_.end(), [](auto &t) { return t.done; }))
                mode_ = Mode::ConfirmClear;
            else
                flash("No completed todos to clear", false);
        }
        else if (k.is("/"))
            mode_ = Mode::Search;
        else if (k.type == Key::Esc && !search_.text().empty())
        {
            search_.set("");
            syncSelection();
        }
        else if (k.is("R") || k.ctrl('r'))
        {
            reload();
            flash("Refreshed", false);
            setTheme(theme_.name);  // pick up edits to the theme file
        }
        else if (k.is("T"))
            openThemePicker();
        else if (focus_ == Focus::Categories && wide_)
            handleCategoryKeys(k);
        else
            handleTodoKeys(k);

        if (!wide_)
            focus_ = Focus::Todos;  // categories panel is hidden; use `c` instead
    }

    void handleCategoryKeys(const Key &k)
    {
        const CategoryItem *cat = tree_.find(catSelId_);
        auto needCategory = [&] {
            flash("Select a category first (not All or Inbox)", true);
        };

        if (k.is("j") || k.type == Key::Down)
            moveCatSel(1);
        else if (k.is("k") || k.type == Key::Up)
            moveCatSel(-1);
        else if (k.is("g") || k.type == Key::Home)
            moveCatSel(-1'000'000);
        else if (k.is("G") || k.type == Key::End)
            moveCatSel(1'000'000);
        else if (k.type == Key::Enter)
            focus_ = Focus::Todos;
        else if (k.is(" "))
        {
            if (cat && !tree_.children(cat->id).empty())
            {
                if (!collapsed_.erase(cat->id))
                    collapsed_.insert(cat->id);
            }
        }
        else if (k.is("a"))
        {
            inputParent_ = target();
            startInput(Mode::AddCategory, "");
        }
        else if (k.is("e"))
            cat ? startInput(Mode::RenameCategory, cat->name) : needCategory();
        else if (k.is("m"))
            cat ? openPicker(PickFor::MoveCategory, cat->id) : needCategory();
        else if (k.is("d") || k.is("x") || k.type == Key::Del)
            cat ? void(mode_ = Mode::ConfirmDeleteCategory) : needCategory();
    }

    void handleTodoKeys(const Key &k)
    {
        const TodoItem *cur = selected();
        if (k.is("j") || k.type == Key::Down)
            moveSel(1);
        else if (k.is("k") || k.type == Key::Up)
            moveSel(-1);
        else if (k.is("g") || k.type == Key::Home)
            moveSel(-1'000'000);
        else if (k.is("G") || k.type == Key::End)
            moveSel(1'000'000);
        else if (k.type == Key::PgDn || k.ctrl('d'))
            moveSel(std::max(rows_ / 2, 1));
        else if (k.type == Key::PgUp || k.ctrl('u'))
            moveSel(-std::max(rows_ / 2, 1));
        else if (k.is("a"))
            startInput(Mode::AddTodo, "");
        else if (!cur)
            return;
        else if (k.is(" ") || k.type == Key::Enter)
        {
            int64_t id = cur->id;
            bool done = !cur->done;
            act([&] { api_.setDone(id, done); });
        }
        else if (k.is("e"))
        {
            editId_ = cur->id;
            startInput(Mode::EditTodo, cur->title);
        }
        else if (k.is("m"))
            openPicker(PickFor::MoveTodo, cur->id);
        else if (k.is("d") || k.is("x") || k.type == Key::Del)
        {
            editId_ = cur->id;
            mode_ = Mode::ConfirmDeleteTodo;
        }
    }

    void handleInput(const Key &k)
    {
        if (k.type == Key::Esc)
        {
            mode_ = Mode::Normal;
            return;
        }
        if (k.type != Key::Enter)
        {
            input_.handle(k);
            return;
        }
        std::string text = trim(input_.text());
        if (text.empty())
        {
            flash(mode_ == Mode::AddTodo || mode_ == Mode::EditTodo ? "Title can't be empty"
                                                                    : "Name can't be empty",
                  true);
            return;
        }

        bool ok = true;
        switch (mode_)
        {
            case Mode::AddTodo:
            {
                const int64_t cat = target();
                ok = act([&] {
                    selId_ = api_.add(text, cat).id;
                    // Make sure the new todo is visible and selected.
                    if (status_ == Status::Done)
                        status_ = Status::All;
                    search_.set("");
                    focus_ = Focus::Todos;
                    flash("Added #" + std::to_string(selId_), false);
                });
                break;
            }
            case Mode::EditTodo:
            {
                const int64_t id = editId_;
                ok = act([&] { api_.rename(id, text); });
                break;
            }
            case Mode::AddCategory:
            {
                const int64_t parent = inputParent_;
                int64_t created = 0;
                ok = act([&] {
                    created = api_.addCategory(text, parent).id;
                    flash("Created " + (parent ? tree_.path(parent) + "/" : "") + text, false);
                });
                if (ok)
                    selectCategory(created);
                break;
            }
            case Mode::RenameCategory:
            {
                const int64_t id = catSelId_;
                ok = act([&] { api_.renameCategory(id, text); });
                break;
            }
            default: break;
        }
        if (ok)  // on failure keep the popup open so the text can be fixed
            mode_ = Mode::Normal;
        syncSelection();
    }

    void handleSearch(const Key &k)
    {
        if (k.type == Key::Esc)
        {
            search_.set("");
            mode_ = Mode::Normal;
        }
        else if (k.type == Key::Enter)
            mode_ = Mode::Normal;
        else if (k.type == Key::Down)
            moveSel(1);
        else if (k.type == Key::Up)
            moveSel(-1);
        else if (search_.handle(k))
        {
            sel_ = 0;
            auto v = visible();
            if (!v.empty())
                selId_ = v[0]->id;
        }
        syncSelection();
    }

    void handleConfirm(const Key &k)
    {
        if (k.is("n") || k.is("N") || k.is("q") || k.type == Key::Esc)
        {
            mode_ = Mode::Normal;
            return;
        }
        if (!(k.is("y") || k.is("Y") || k.type == Key::Enter))
            return;

        if (mode_ == Mode::ConfirmDeleteTodo)
        {
            const int64_t id = editId_;
            act([&] {
                api_.remove(id);
                flash("Deleted #" + std::to_string(id), false);
            });
        }
        else if (mode_ == Mode::ConfirmDeleteCategory)
        {
            const int64_t id = catSelId_;
            const auto *c = tree_.find(id);
            const int64_t parent = c ? c->parentId : 0;
            const std::string name = tree_.path(id);
            if (act([&] { api_.removeCategory(id); }))
            {
                flash("Deleted " + name, false);
                selectCategory(parent ? parent : kAll);
            }
        }
        else
        {
            act([&] {
                auto n = api_.clearDone();
                flash("Cleared " + std::to_string(n) + " completed todo(s)", false);
            });
        }
        mode_ = Mode::Normal;
    }

    // ── drawing ──

    void panel(int x, int y, int w, int h, const std::string &title, bool active,
               const std::string &footer = "")
    {
        Style border = active ? Style(theme_.borderActive, Default, Bold) : Style(theme_.border);
        c_.box(x, y, w, h, border);
        c_.text(x + 1, y, "─" + truncate(title, w - 4) + "─", border, x + w - 1);
        if (!footer.empty() && w > 6)
        {
            auto f = truncate(footer, w - 4);
            c_.text(x + w - 2 - textWidth(f), y + h - 1, f, border);
        }
    }

    // Keeps `sel` inside a window of `rows` lines starting at `offset`.
    static void scrollTo(int &offset, size_t sel, int rows, size_t count)
    {
        if (static_cast<int>(sel) < offset)
            offset = static_cast<int>(sel);
        if (static_cast<int>(sel) >= offset + rows)
            offset = static_cast<int>(sel) - rows + 1;
        offset = std::clamp(offset, 0, std::max(0, static_cast<int>(count) - rows));
    }

    void draw(Terminal &term)
    {
        int W, H;
        term.size(W, H);
        c_.reset(W, H);
        c_.setDefaults(theme_.fg, theme_.bg);
        if (gResized)
        {
            gResized = 0;
            lastFrame_.clear();
            term.write("\033[2J");
        }

        int cx = -1, cy = -1;  // where to show the text cursor, if anywhere
        if (W < 30 || H < 8)
        {
            c_.text(0, 0, "Terminal too small", Style(theme_.pending));
        }
        else
        {
            syncSelection();
            const int main = H - 1;
            wide_ = W >= 70 && main >= 14;
            if (!wide_)
                focus_ = Focus::Todos;
            const int left = wide_ ? std::clamp(W / 4, 24, 36) : 0;
            const int details = wide_ ? 6 : 0;

            if (wide_)
            {
                drawStatus(0, 0, left, 4);
                drawCategories(0, 4, left, main - 4);
                drawDetails(left, main - details, W - left, details);
            }
            drawTodos(left, 0, W - left, main - details);
            drawBottomBar(main, W, cx, cy);

            switch (mode_)
            {
                case Mode::AddTodo:
                case Mode::EditTodo:
                case Mode::AddCategory:
                case Mode::RenameCategory: drawInputPopup(W, H, cx, cy); break;
                case Mode::ConfirmDeleteTodo:
                case Mode::ConfirmDeleteCategory:
                case Mode::ConfirmClear: drawConfirmPopup(W, H); break;
                case Mode::Pick: drawPickPopup(W, H); break;
                case Mode::Help: drawHelpPopup(W, H); break;
                default: break;
            }
        }

        std::string frame = c_.render();
        if (cx >= 0)
            frame += "\033[" + std::to_string(cy + 1) + ";" + std::to_string(cx + 1) + "H\033[?25h";
        else
            frame += "\033[?25l";
        if (frame != lastFrame_)
        {
            term.write(frame);
            lastFrame_ = std::move(frame);
        }
    }

    void drawStatus(int x, int y, int w, int h)
    {
        panel(x, y, w, h, "Status", false);
        const int32_t state = online_ ? theme_.success : theme_.error;
        int e = c_.text(x + 2, y + 1, "● ", Style(state));
        c_.text(e, y + 1, online_ ? "online" : "offline", Style(state, Default, Bold));
        std::string host = api_.server();
        if (auto p = host.find("://"); p != std::string::npos)
            host = host.substr(p + 3);
        c_.text(x + 2, y + 2, truncate(host, w - 4), muted());
    }

    void drawCategories(int x, int y, int w, int h)
    {
        const bool active = focus_ == Focus::Categories;
        panel(x, y, w, h, "[1] Categories", active);
        auto rows = catRows();
        const int visibleRows = h - 2;
        if (visibleRows <= 0)
            return;
        scrollTo(catOffset_, catSel_, visibleRows, rows.size());

        const int maxX = x + w - 1;
        for (int r = 0; r < visibleRows && catOffset_ + r < static_cast<int>(rows.size()); ++r)
        {
            const size_t idx = static_cast<size_t>(catOffset_ + r);
            const CatRow &row = rows[idx];
            const int line = y + 1 + r;
            const bool sel = idx == catSel_;
            const int32_t bg = sel ? (active ? theme_.selectionBg : theme_.selectionBgInactive) : Default;
            if (sel)
                c_.fill(x + 1, line, w - 2, 1, Style(Default, bg));

            const std::string marker = row.hasChildren
                                           ? (collapsed_.count(row.id) ? "▸ " : "▾ ")
                                           : "  ";
            int cx = c_.text(x + 2 + 2 * row.depth, line, marker,
                             sel ? Style(theme_.selectionFg, bg) : muted(bg), maxX);
            auto count = openCount(row.id);
            auto n = count ? std::to_string(count) : "";
            const int countX = maxX - 1 - static_cast<int>(n.size());
            const bool pseudo = row.id <= 0;
            c_.text(cx, line, truncate(row.name, countX - 1 - cx),
                    Style(sel ? theme_.selectionFg : pseudo ? Default : theme_.category, bg,
                          sel ? Bold : 0),
                    maxX);
            c_.text(countX, line, n, sel ? Style(theme_.selectionFg, bg) : muted(bg), maxX);
        }
    }

    void drawTodos(int x, int y, int w, int h)
    {
        const bool active = focus_ == Focus::Todos;
        auto v = visible();
        std::string title = "[2] " + viewName(catSelId_);
        if (status_ != Status::All)
            title += " · " + std::string(kStatusNames[static_cast<int>(status_)]);
        if (!search_.text().empty())
            title += " · /" + search_.text();
        panel(x, y, w, h, title, active,
              v.empty() ? "" : std::to_string(sel_ + 1) + " of " + std::to_string(v.size()));

        rows_ = h - 2;
        if (rows_ <= 0)
            return;
        if (v.empty())
        {
            std::string msg = todos_.empty() ? "No todos yet — press a to add one"
                              : search_.text().empty() ? "Nothing here — press a to add a todo"
                                                       : "Nothing matches";
            msg = truncate(msg, w - 4);
            c_.text(x + (w - textWidth(msg)) / 2, y + 1 + rows_ / 2, msg, muted());
            return;
        }
        scrollTo(offset_, sel_, rows_, v.size());

        int idWidth = 1;
        for (auto *t : v)
            idWidth = std::max(idWidth, static_cast<int>(std::to_string(t->id).size()));

        const int maxX = x + w - 1;
        for (int r = 0; r < rows_ && offset_ + r < static_cast<int>(v.size()); ++r)
        {
            const size_t idx = static_cast<size_t>(offset_ + r);
            const TodoItem &t = *v[idx];
            const int row = y + 1 + r;
            const bool sel = idx == sel_;
            const int32_t bg = sel ? (active ? theme_.selectionBg : theme_.selectionBgInactive) : Default;
            if (sel)
                c_.fill(x + 1, row, w - 2, 1, Style(Default, bg));

            int cx = c_.text(x + 2, row, t.done ? "[x]" : "[ ]",
                             Style(t.done ? theme_.done : theme_.pending, bg, Bold), maxX);
            auto id = std::to_string(t.id);
            cx = c_.text(cx + 1 + idWidth - static_cast<int>(id.size()), row, id,
                         sel ? Style(theme_.selectionFg, bg) : muted(bg), maxX);

            // Where the todo lives, when that isn't the category being viewed.
            std::string tag;
            if (t.categoryId != 0 && t.categoryId != catSelId_)
                tag = catSelId_ > 0 ? tree_.relativePath(t.categoryId, catSelId_)
                                    : tree_.path(t.categoryId);
            tag = truncate(tag, (w - 4) / 3);
            const int tagX = maxX - 1 - textWidth(tag);
            if (!tag.empty())
                c_.text(tagX, row, tag,
                        sel ? Style(theme_.selectionFg, bg) : Style(theme_.category, bg, Dim), maxX);

            const uint8_t strike = t.done ? Strike : 0;
            const int titleEnd = tag.empty() ? maxX - 1 : tagX - 2;
            c_.text(cx + 2, row, truncate(t.title, titleEnd - (cx + 2)),
                    sel      ? Style(theme_.selectionFg, bg, Bold | strike)
                    : t.done ? muted(bg, strike)
                             : Style(Default, bg),
                    maxX);
        }
    }

    void drawDetails(int x, int y, int w, int h)
    {
        panel(x, y, w, h, "Details", false);
        const TodoItem *t = selected();
        const int inner = w - 4;
        const int maxX = x + w - 1;
        if (!t)
        {
            c_.text(x + 2, y + 1, "No todo selected", muted(), maxX);
            return;
        }
        const Style dim = muted();
        int cx = c_.text(x + 2, y + 1, "#" + std::to_string(t->id), Style(Default, Default, Bold));
        cx = c_.text(cx, y + 1, " · ", dim);
        cx = c_.text(cx, y + 1, t->done ? "done" : "pending", Style(t->done ? theme_.done : theme_.pending));
        cx = c_.text(cx, y + 1, " · ", dim);
        cx = c_.text(cx, y + 1, viewName(t->categoryId), Style(theme_.category), maxX);
        c_.text(cx, y + 1, " · created " + formatTime(t->createdAt), dim, maxX);

        auto lines = wrap(t->title, inner);
        const int avail = h - 3;
        for (int i = 0; i < avail && i < static_cast<int>(lines.size()); ++i)
        {
            std::string line = lines[i];
            if (i == avail - 1 && static_cast<int>(lines.size()) > avail)
                line = truncate(line + "  …", inner);
            c_.text(x + 2, y + 2 + i, line, Style(), maxX);
        }
    }

    void keyHints(int y, int w, const std::vector<std::pair<const char *, const char *>> &hints)
    {
        int cx = 1;
        for (const auto &[key, desc] : hints)
        {
            int need = textWidth(key) + 1 + textWidth(desc) + 3;
            if (cx + need > w)
                break;
            cx = c_.text(cx, y, key, Style(theme_.accent, Default, Bold));
            cx = c_.text(cx + 1, y, desc, Style()) + 3;
        }
    }

    void drawBottomBar(int y, int w, int &cx, int &cy)
    {
        if (mode_ == Mode::Search)
        {
            int x = c_.text(1, y, "search: ", Style(theme_.accent, Default, Bold));
            cx = search_.draw(c_, x, y, w - x - 1, Style(theme_.search));
            cy = y;
            return;
        }
        if (Clock::now() < flashUntil_)
        {
            c_.text(1, y, truncate(flash_, w - 2), Style(flashError_ ? theme_.error : theme_.success, Default, Bold));
            return;
        }
        if (focus_ == Focus::Categories)
            keyHints(y, w,
                     {{"a", "add subcategory"},
                      {"e", "rename"},
                      {"m", "move"},
                      {"d", "delete"},
                      {"space", "fold"},
                      {"n", "new todo"},
                      {"?", "help"},
                      {"q", "quit"}});
        else
            keyHints(y, w,
                     {{"a", "add"},
                      {"e", "edit"},
                      {"space", "toggle"},
                      {"m", "move"},
                      {"d", "delete"},
                      {"c", "category"},
                      {"/", "search"},
                      {"H", "hide done"},
                      {"[ ]", "filter"},
                      {"?", "help"},
                      {"q", "quit"}});
    }

    std::pair<int, int> popup(int W, int H, int w, int h, const std::string &title,
                              const std::string &footer)
    {
        w = std::min(w, W - 2);
        h = std::min(h, H - 2);
        int x = (W - w) / 2, y = (H - h) / 2;
        c_.fill(x, y, w, h, Style());
        panel(x, y, w, h, title, true, footer);
        return {x, y};
    }

    void drawInputPopup(int W, int H, int &cx, int &cy)
    {
        const int w = std::min(W - 4, 64);
        std::string title;
        switch (mode_)
        {
            case Mode::AddTodo:
                title = target() ? "New todo in " + tree_.path(target()) : "New todo";
                break;
            case Mode::EditTodo: title = "Edit #" + std::to_string(editId_); break;
            case Mode::AddCategory:
                title = inputParent_ ? "New category in " + tree_.path(inputParent_)
                                     : "New category";
                break;
            default: title = "Rename " + tree_.path(catSelId_); break;
        }
        auto [x, y] = popup(W, H, w, 3, title, " enter save · esc cancel ");
        cx = input_.draw(c_, x + 2, y + 1, w - 4, Style());
        cy = y + 1;
    }

    void drawConfirmPopup(int W, int H)
    {
        const int w = std::min(W - 4, 60);
        std::vector<std::string> lines;
        std::string title = "Delete";
        if (mode_ == Mode::ConfirmClear)
        {
            title = "Clear completed";
            auto n = std::count_if(todos_.begin(), todos_.end(), [](auto &t) { return t.done; });
            lines.push_back("Delete " + std::to_string(n) + " completed todo(s)?");
        }
        else if (mode_ == Mode::ConfirmDeleteCategory)
        {
            const auto *c = tree_.find(catSelId_);
            const std::string name = c ? c->name : "?";
            const int64_t parent = c ? c->parentId : 0;
            lines.push_back("Delete category “" + truncate(name, w - 22) + "”?");
            lines.push_back("Its todos and subcategories move to " +
                            (parent ? tree_.path(parent) : std::string("the top level")) + ".");
        }
        else
        {
            std::string name = "#" + std::to_string(editId_);
            for (const auto &t : todos_)
                if (t.id == editId_)
                    name = t.title;
            lines.push_back("Delete “" + truncate(name, w - 16) + "”?");
        }
        auto [x, y] = popup(W, H, w, static_cast<int>(lines.size()) + 2, title,
                            " y confirm · n cancel ");
        for (size_t i = 0; i < lines.size(); ++i)
            c_.text(x + 2, y + 1 + static_cast<int>(i), lines[i],
                    i == 0 ? Style(Default, Default, Bold) : muted(), x + w - 1);
    }

    void drawPickPopup(int W, int H)
    {
        const int w = std::min(W - 4, 50);
        const int h = std::min(H - 4, static_cast<int>(pickOptions_.size()) + 2);
        std::string title = "Show category";
        if (pickFor_ == PickFor::Theme)
            title = "Theme";
        else if (pickFor_ == PickFor::MoveTodo)
            title = "Move #" + std::to_string(pickSubject_) + " to";
        else if (pickFor_ == PickFor::MoveCategory)
        {
            const auto *c = tree_.find(pickSubject_);
            title = "Move “" + (c ? c->name : std::string("?")) + "” into";
        }
        auto [x, y] = popup(W, H, w, h, title, " enter choose · esc cancel ");
        const int rows = h - 2;
        scrollTo(pickOffset_, pickSel_, rows, pickOptions_.size());
        for (int r = 0; r < rows && pickOffset_ + r < static_cast<int>(pickOptions_.size()); ++r)
        {
            const size_t idx = static_cast<size_t>(pickOffset_ + r);
            const auto &opt = pickOptions_[idx];
            const bool sel = idx == pickSel_;
            const int line = y + 1 + r;
            const int32_t bg = sel ? theme_.selectionBg : Default;
            if (sel)
                c_.fill(x + 1, line, w - 2, 1, Style(Default, bg));
            const bool isCategory = pickFor_ != PickFor::Theme && opt.id > 0;
            c_.text(x + 2 + 2 * opt.depth, line, opt.label,
                    Style(sel ? theme_.selectionFg : isCategory ? theme_.category : Default, bg,
                          sel ? Bold : 0),
                    x + w - 1);
        }
    }

    void drawHelpPopup(int W, int H)
    {
        const int w = 50;
        auto [x, y] = popup(W, H, w, static_cast<int>(kHelp.size()) + 2, "Keybindings",
                            " any key to close ");
        for (size_t i = 0; i < kHelp.size() && static_cast<int>(i) < H - 4; ++i)
        {
            const int row = y + 1 + static_cast<int>(i);
            c_.text(x + 2, row, kHelp[i].first, Style(theme_.accent, Default, Bold));
            c_.text(x + 18, row, kHelp[i].second, Style(), x + w - 1);
        }
    }

    Api &api_;
    Canvas c_;
    Theme theme_;
    std::vector<std::string> themeNames_;  // theme picker options
    std::string themeBefore_;              // restored if the picker is cancelled
    std::string lastFrame_;
    bool wide_{true};

    std::vector<TodoItem> todos_;
    CategoryTree tree_;
    bool online_{true};
    Clock::time_point lastLoad_{};

    Mode mode_{Mode::Normal};
    Focus focus_{Focus::Todos};
    Status status_{Status::All};

    size_t sel_{0};  // todo selection
    int64_t selId_{0};
    int offset_{0};
    int rows_{1};

    size_t catSel_{0};  // category panel selection
    int64_t catSelId_{kAll};
    int catOffset_{0};
    std::set<int64_t> collapsed_;

    std::vector<PickOption> pickOptions_;
    PickFor pickFor_{PickFor::ShowCategory};
    int64_t pickSubject_{0};
    size_t pickSel_{0};
    int pickOffset_{0};

    LineEdit input_;
    LineEdit search_;
    int64_t editId_{0};
    int64_t inputParent_{0};

    std::string flash_;
    bool flashError_{false};
    Clock::time_point flashUntil_{};

    bool quit_{false};
};
}  // namespace

int runTui(Api &api)
{
    std::setlocale(LC_CTYPE, "");  // for wcwidth() on non-ASCII titles
    return App(api).run();
}
