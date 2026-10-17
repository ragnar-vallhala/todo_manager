// Lazygit-style terminal UI, drawn with plain ANSI escapes (no ncurses).
#include "Tui.h"
#include "Api.h"
#include "Keys.h"
#include "Theme.h"

#include <drogon/utils/Utilities.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <clocale>
#include <cmath>
#include <csignal>
#include <ctime>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <set>
#include <sstream>
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
    Italic = 8,
    Underline = 16,
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
        if (attrs & Italic)
            r += ";3";
        if (attrs & Underline)
            r += ";4";
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

termios gOrigTermios;
volatile sig_atomic_t gRawMode = 0;
volatile sig_atomic_t gResized = 0;

// "\033[>1u" asks terminals that support it to report keys unambiguously (the
// kitty protocol), so Shift+Enter is told apart from Enter; others ignore it.
// "\033[?2004h" turns on bracketed paste, so a pasted newline isn't read as
// Enter (posting half the paste) and pasted letters aren't run as commands.
constexpr char kEnterScreen[] = "\033[?1049h\033[?25l\033[?7l\033[>1u\033[?2004h";
constexpr char kLeaveScreen[] = "\033[?2004l\033[<u\033[0m\033[?7h\033[?25h\033[?1049l";

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
        enterRaw();

        for (int sig : {SIGTERM, SIGHUP, SIGINT, SIGQUIT})
            std::signal(sig, onFatalSignal);
        struct sigaction sa{};
        sa.sa_handler = onResize;  // no SA_RESTART: interrupts poll()
        sigaction(SIGWINCH, &sa, nullptr);
    }
    ~Terminal() { restoreTerminal(); }

    // Hands the terminal back (to run an editor, say) and takes it again.
    void suspend() { restoreTerminal(); }
    void resume()
    {
        enterRaw();
        buf_.clear();
    }

  private:
    void enterRaw()
    {
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
    }

  public:

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

    static constexpr char kPasteStart[] = "\033[200~";
    static constexpr char kPasteEnd[] = "\033[201~";

    // True if buf_ begins a bracketed paste (waiting briefly for the rest of
    // the marker when only part of it has arrived).
    bool startsPaste()
    {
        const std::string start = kPasteStart;
        while (buf_.size() < start.size() && start.compare(0, buf_.size(), buf_) == 0)
            if (!fill(30))
                return false;
        return buf_.compare(0, start.size(), start) == 0;
    }

    // Reads up to the end marker and returns the text as one Paste key. Line
    // breaks become "\n" whatever the terminal sent (\r\n, \r, ESC E or NEL);
    // other control characters are dropped.
    Key readPaste()
    {
        const std::string end = kPasteEnd;
        size_t pos;
        while ((pos = buf_.find(end, sizeof(kPasteStart) - 1)) == std::string::npos)
            if (!fill(500))
            {
                pos = buf_.size();  // the end marker never came: take what we have
                break;
            }
        const std::string raw = buf_.substr(sizeof(kPasteStart) - 1, pos - (sizeof(kPasteStart) - 1));
        buf_.erase(0, std::min(buf_.size(), pos + end.size()));

        Key k{Key::Paste};
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const unsigned char c = raw[i];
            if (c == '\r' || c == '\n')
            {
                k.ch += '\n';
                if (c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
                    ++i;
            }
            else if (c == 0x1B && i + 1 < raw.size() && raw[i + 1] == 'E')
            {
                k.ch += '\n';
                ++i;
            }
            else if (c == 0xC2 && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0x85)
            {
                k.ch += '\n';
                ++i;
            }
            else if (c == '\t')
                k.ch += ' ';
            else if (c >= 0x20 && c != 0x7F)
                k.ch += static_cast<char>(c);
        }
        return k;
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
        if (b == 0x1B && startsPaste())
            return readPaste();
        if (b == 0x1B)
        {
            if (buf_.size() == 1 && !fill(30))
                return take(1, Key::Esc);
            if (buf_[1] != '[' && buf_[1] != 'O')
            {
                // Alt+x arrives as Esc, x.
                const unsigned char c = buf_[1];
                if (c == '\r')
                {
                    buf_.erase(0, 2);
                    Key k{Key::Enter};
                    k.mods = Key::Alt;
                    return k;
                }
                if (c < 0x20 || c == 0x7F)
                    return take(1, Key::Esc);
                buf_.erase(0, 1);
                Key k = parse();
                k.mods |= Key::Alt;
                return k;
            }
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
            // "1;5" = key 1 with modifiers 5: 1 + shift 1, alt 2, ctrl 4, super 8.
            const std::string params = buf_.substr(2, i - 2);
            const int param = std::atoi(params.c_str());
            const auto semi = params.find(';');
            const int modParam = semi == std::string::npos ? 1 : std::atoi(params.c_str() + semi + 1);
            const auto mods = static_cast<uint8_t>(modParam > 1 ? (modParam - 1) & 0xF : 0);
            char fin = buf_[i];
            buf_.erase(0, i + 1);
            auto key = [mods](Key::Type t) {
                Key k{t};
                k.mods = mods;
                return k;
            };
            switch (fin)
            {
                case 'A': return key(Key::Up);
                case 'B': return key(Key::Down);
                case 'C': return key(Key::Right);
                case 'D': return key(Key::Left);
                case 'H': return key(Key::Home);
                case 'F': return key(Key::End);
                case 'Z': return {Key::BackTab};
                case '~':
                    switch (param)
                    {
                        case 1: case 7: return key(Key::Home);
                        case 4: case 8: return key(Key::End);
                        case 3: return key(Key::Del);
                        case 5: return key(Key::PgUp);
                        case 6: return key(Key::PgDn);
                        case 27:  // xterm's modifyOtherKeys: "CSI 27;mods;code ~"
                            if (std::atoi(params.c_str() + params.rfind(';') + 1) == 13)
                                return key(Key::Enter);
                            break;
                    }
                    break;
                case 'u':  // "CSI code;mods u", from terminals that report every key
                {
                    if (param == 13)
                        return key(Key::Enter);
                    if (param == 9)
                        return key(mods & Key::Shift ? Key::BackTab : Key::Tab);
                    if (param == 27)
                        return key(Key::Esc);
                    if (param == 127)
                        return key(Key::Backspace);
                    if (param < 0x20 || param > 0x7E)
                        return {};
                    Key k = key(Key::Char);
                    k.ch = std::string(1, static_cast<char>(param));
                    if ((k.mods & Key::CtrlMod) && std::isalpha(param))
                    {
                        k.type = Key::Ctrl;
                        k.ch = std::string(1, static_cast<char>(std::tolower(param)));
                        k.mods &= ~Key::CtrlMod;
                    }
                    else if ((k.mods & Key::Shift) && std::isalpha(param))
                        k.ch = std::string(1, static_cast<char>(std::toupper(param)));
                    return k;
                }
            }
            return {};  // unknown sequence: ignore
        }
        if (b == '\r')
            return take(1, Key::Enter);
        if (b == '\n')  // Ctrl+J, now that Enter sends \r
        {
            Key k = take(1, Key::Enter);
            k.mods = Key::CtrlMod;
            return k;
        }
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
            case Key::Paste: paste(k.ch, false); return true;
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
            case Key::Home: cur_ = lineStart(); return true;
            case Key::End: cur_ = lineEnd(); return true;
            case Key::Up:
            case Key::Down:
            {
                // To the same column of the line above / below, if there is one.
                const size_t col = cur_ - lineStart();
                if (k.type == Key::Up ? lineStart() == 0 : lineEnd() == chars_.size())
                    return false;
                cur_ = k.type == Key::Up ? lineStart() - 1 : lineEnd() + 1;
                cur_ = std::min(lineStart() + col, lineEnd());
                return true;
            }
            case Key::Ctrl:
                switch (k.ch[0])
                {
                    case 'a': cur_ = lineStart(); return true;
                    case 'e': cur_ = lineEnd(); return true;
                    case 'u':
                    {
                        const size_t b = lineStart();
                        chars_.erase(chars_.begin() + b, chars_.begin() + cur_);
                        cur_ = b;
                        return true;
                    }
                    case 'k': chars_.erase(chars_.begin() + cur_, chars_.begin() + lineEnd()); return true;
                    case 'w':
                        while (cur_ > 0 && chars_[cur_ - 1] == " ")
                            chars_.erase(chars_.begin() + --cur_);
                        while (cur_ > 0 && !isSpace(chars_[cur_ - 1]))
                            chars_.erase(chars_.begin() + --cur_);
                        return true;
                }
                return false;
            default: return false;
        }
    }

    void newline() { chars_.insert(chars_.begin() + cur_++, "\n"); }

    // Inserts pasted text at the cursor; without `multiline` its line breaks
    // become spaces.
    void paste(const std::string &text, bool multiline)
    {
        for (const auto &g : splitUtf8(text))
            chars_.insert(chars_.begin() + cur_++, g == "\n" && !multiline ? " " : g);
    }

    // The word the cursor is at the end of ("" after a space).
    std::string wordBeforeCursor() const
    {
        size_t b = cur_;
        while (b > 0 && !isSpace(chars_[b - 1]))
            --b;
        std::string w;
        for (size_t i = b; i < cur_; ++i)
            w += chars_[i];
        return w;
    }

    // Replaces that word with `word`.
    void replaceWordBeforeCursor(const std::string &word)
    {
        size_t b = cur_;
        while (b > 0 && !isSpace(chars_[b - 1]))
            --b;
        chars_.erase(chars_.begin() + b, chars_.begin() + cur_);
        auto add = splitUtf8(word);
        chars_.insert(chars_.begin() + b, add.begin(), add.end());
        cur_ = b + add.size();
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

    // Multi-line text wrapped to `width` columns: how many rows it takes.
    int rows(int width) const { return static_cast<int>(layout(width).rows.size()); }

    // Draws at most `height` rows of it at (x, y), scrolled so the cursor
    // is visible. Returns the cursor's screen column and row.
    std::pair<int, int> drawWrapped(Canvas &c, int x, int y, int width, int height, Style st) const
    {
        const Layout l = layout(width);
        const int top = std::max(0, l.curRow - height + 1);
        for (int r = 0; r < height && top + r < static_cast<int>(l.rows.size()); ++r)
        {
            const auto [from, to] = l.rows[top + r];
            int cx = x;
            for (size_t i = from; i < to; ++i)
                cx = c.text(cx, y + r, chars_[i], st, x + width);
        }
        return {x + l.curCol, y + l.curRow - top};
    }

  private:
    static bool isSpace(const std::string &g) { return g == " " || g == "\n"; }

    size_t lineStart() const
    {
        size_t b = cur_;
        while (b > 0 && chars_[b - 1] != "\n")
            --b;
        return b;
    }

    size_t lineEnd() const
    {
        size_t e = cur_;
        while (e < chars_.size() && chars_[e] != "\n")
            ++e;
        return e;
    }

    struct Layout
    {
        std::vector<std::pair<size_t, size_t>> rows;  // [from, to) in chars_
        int curRow{0}, curCol{0};
    };

    // Breaks at newlines, and at the width (keeping a column for the cursor).
    Layout layout(int width) const
    {
        Layout l;
        size_t from = 0;
        int col = 0;
        for (size_t i = 0; i <= chars_.size(); ++i)
        {
            const bool end = i == chars_.size() || chars_[i] == "\n";
            const int cw = end ? 0 : charWidth(chars_[i]);
            if (!end && col + cw > width - 1 && i > from)
            {
                l.rows.push_back({from, i});
                from = i;
                col = 0;
            }
            if (i == cur_)
            {
                l.curRow = static_cast<int>(l.rows.size());
                l.curCol = col;
            }
            if (end)
            {
                l.rows.push_back({from, i});
                from = i + 1;
                col = 0;
            }
            else
                col += cw;
        }
        return l;
    }

    std::vector<std::string> chars_;
    size_t cur_{0};
};

// ────────────────────────── Attachments ─────────────────────────

// Images show inside the UI in kitty (its graphics protocol); elsewhere,
// and for PDFs, files open in the desktop's viewer.
bool inKitty()
{
    const char *term = std::getenv("TERM");
    return std::getenv("KITTY_WINDOW_ID") || (term && std::string(term) == "xterm-kitty");
}

// Where opened attachments are kept: $XDG_CACHE_HOME/todo/files, else
// ~/.cache/todo/files.
std::filesystem::path fileCacheDir()
{
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "todo" / "files";
    const char *home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".cache" / "todo" / "files";
}

std::string shellQuote(const std::string &s)
{
    std::string out = "'";
    for (char ch : s)
        out += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return out + "'";
}

// Whether `program` is on the PATH.
bool onPath(const std::string &program)
{
    const char *path = std::getenv("PATH");
    std::istringstream dirs(path ? path : "");
    for (std::string dir; std::getline(dirs, dir, ':');)
        if (!dir.empty() && access((dir + "/" + program).c_str(), X_OK) == 0)
            return true;
    return false;
}

// Runs `opener path` (xdg-open unless said) in the background, detached
// from the terminal, so the viewer's output doesn't land on the screen.
void openExternally(const std::string &path, const char *opener = "xdg-open")
{
    const pid_t pid = fork();
    if (pid == 0)
    {
        setsid();
        if (fork() == 0)
        {
            const int null = ::open("/dev/null", O_RDWR);
            dup2(null, STDIN_FILENO);
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
            execlp(opener, opener, path.c_str(), static_cast<char *>(nullptr));
        }
        _exit(0);
    }
    if (pid > 0)
        waitpid(pid, nullptr, 0);
}

// Saves an image on the clipboard (a screenshot, say) as a PNG in the cache
// and returns its path; "" if the clipboard holds no image. Terminals only
// paste text, so this asks wl-paste (Wayland) or xclip (X11) directly.
std::string clipboardImage()
{
    const bool wayland = std::getenv("WAYLAND_DISPLAY") != nullptr;
    const std::string types = wayland ? "wl-paste --list-types 2>/dev/null"
                                      : "xclip -selection clipboard -t TARGETS -o 2>/dev/null";
    FILE *t = popen(types.c_str(), "r");
    if (!t)
        return "";
    bool png = false;
    char line[256];
    while (fgets(line, sizeof line, t))
        png = png || std::string(line).rfind("image/png", 0) == 0;
    pclose(t);
    if (!png)
        return "";

    std::error_code ec;
    const auto dir = fileCacheDir().parent_path() / "clipboard";
    std::filesystem::create_directories(dir, ec);
    char name[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof name, "screenshot-%Y%m%d-%H%M%S.png", std::localtime(&now));
    const std::string path = (dir / name).string();
    const std::string get = (wayland ? "wl-paste --type image/png" : "xclip -selection clipboard -t image/png -o") +
                            std::string(" > ") + shellQuote(path) + " 2>/dev/null";
    if (std::system(get.c_str()) != 0 || std::filesystem::file_size(path, ec) == 0 || ec)
        return "";
    return path;
}

// A PNG's size from its header; 0 x 0 if it isn't one.
std::pair<int, int> pngSize(const std::string &png)
{
    if (png.size() < 24 || png.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0)
        return {0, 0};
    auto be32 = [&](size_t i) {
        return static_cast<int>(static_cast<unsigned char>(png[i]) << 24 |
                                static_cast<unsigned char>(png[i + 1]) << 16 |
                                static_cast<unsigned char>(png[i + 2]) << 8 |
                                static_cast<unsigned char>(png[i + 3]));
    };
    return {be32(16), be32(20)};
}

// "717 B", "18 KB", "2.4 MB".
std::string sizeLabel(int64_t n)
{
    char buf[32];
    if (n < 1024)
        std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(n));
    else if (n < 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%lld KB", static_cast<long long>(n / 1024));
    else
        std::snprintf(buf, sizeof(buf), "%.1f MB", n / (1024.0 * 1024.0));
    return buf;
}

// ───────────────────────────── App ──────────────────────────────

// Pruned todos are done ones moved aside (ids p1, p2...) to free their ids.
enum class Status { All, Pending, Done, Pruned };
constexpr const char *kStatusNames[] = {"All", "Pending", "Done", "Pruned"};

enum class Mode
{
    Normal,
    AddTodo,
    EditTodo,
    AddCategory,
    RenameCategory,
    StyleCategory,
    SetDeadline,
    AddRoom,
    RenameRoom,
    RoomTopic,
    PostMessage,
    Search,
    Pick,
    ConfirmDeleteTodo,
    ConfirmDeleteCategory,
    ConfirmDeleteRoom,
    ConfirmPruneLog,
    ConfirmClear,
    Help,
    ViewFile,  // an image shown over the UI (kitty only)
};
enum class Focus { Categories, Todos, Details };
enum class PickFor { ShowCategory, MoveTodo, MoveCategory, Priority, Theme, File };

// Pseudo-categories in the categories panel.
constexpr int64_t kAll = -1;      // every todo not in the archive
constexpr int64_t kInbox = 0;     // todos without a category
constexpr int64_t kArchive = -2;  // archived categories, listed last
constexpr int64_t kRooms = -3;    // every todo shared in a room
// Room N's row is kRoomBase - N, so it can't clash with the ones above.
constexpr int64_t kRoomBase = -1000;

bool isRoomView(int64_t view) { return view <= kRoomBase; }
int64_t roomView(int64_t roomId) { return kRoomBase - roomId; }
int64_t roomOf(int64_t view) { return isRoomView(view) ? kRoomBase - view : 0; }

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
    bool archived;
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
    explicit App(Api &api) : api_(api) { collapsed_.insert(kArchive); }

    int run()
    {
        Terminal term;
        term_ = &term;
        setTheme(configuredTheme());
        loadKeys();
        loadLayout();
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
            todos_ = api_.list("all", true);
            pruned_ = api_.listPruned();
            tree_ = api_.categories(true);
            online_ = true;
            try
            {
                rooms_ = api_.rooms();
            }
            catch (const ApiError &e)
            {
                if (e.offline)
                    throw;
                rooms_.clear();  // a server without rooms
            }
            refreshLog();
        }
        catch (const ApiError &e)
        {
            if (online_ || !e.offline)
                flash(e.what(), true);
            online_ = !e.offline;
        }
        syncSelection();
    }

    // Loads the log of the room being viewed: all of it (the last 200) when
    // it's a different room or messages were pruned or restored, else what's
    // new since last time.
    void refreshLog()
    {
        const int64_t room = roomOf(catSelId_);
        if (!room)
        {
            log_.clear();
            logRoom_ = 0;
            return;
        }
        const RoomItem *r = findRoom(room);
        const int64_t version = r ? r->logVersion : 0;
        if (room != logRoom_ || version != logVersion_)
        {
            log_ = api_.messages(room, 0, 200);
            logFollow_ = logFollow_ || room != logRoom_;
            logRoom_ = room;
            logVersion_ = version;
            return;
        }
        auto more = api_.messages(room, log_.empty() ? 0 : log_.back().seq, 200);
        log_.insert(log_.end(), more.begin(), more.end());
        if (log_.size() > 500)
            log_.erase(log_.begin(), log_.end() - 500);
    }

    const RoomItem *findRoom(int64_t id) const
    {
        for (const auto &r : rooms_)
            if (r.id == id)
                return &r;
        return nullptr;
    }

    std::string roomName(int64_t id) const
    {
        const auto *r = findRoom(id);
        return r ? r->name : "room " + std::to_string(id);
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

    // Is todo t shown when category `view` (kAll, kInbox, kArchive or an id)
    // is selected? Archived todos only show inside archived categories.
    bool inView(const TodoItem &t, int64_t view) const
    {
        if (view == kAll)
            return !tree_.inArchive(t.categoryId);
        if (view == kInbox)
            return t.categoryId == 0;
        if (view == kArchive)
            return tree_.inArchive(t.categoryId);
        if (view == kRooms)
            return t.roomId != 0;
        if (isRoomView(view))
            return t.roomId == roomOf(view);
        return tree_.isWithin(t.categoryId, view) &&
               (tree_.inArchive(view) || !tree_.inArchive(t.categoryId));
    }

    bool doneOutsideArchive(const TodoItem &t) const
    {
        return t.done && !tree_.inArchive(t.categoryId);
    }

    size_t openCount(int64_t view) const
    {
        return std::count_if(todos_.begin(), todos_.end(),
                             [&](const TodoItem &t) { return !t.done && inView(t, view); });
    }

    std::string viewName(int64_t view) const
    {
        return view == kAll        ? "All"
               : view == kInbox    ? "Inbox"
               : view == kArchive  ? "Archived"
               : view == kRooms    ? "Rooms"
               : isRoomView(view)  ? "Room " + roomName(roomOf(view))
                                   : tree_.path(view);
    }

    // Where new todos/subcategories go: the selected category, else none.
    int64_t target() const { return catSelId_ > 0 ? catSelId_ : 0; }
    // The room new todos are shared in: the selected one, else none.
    int64_t targetRoom() const { return roomOf(catSelId_); }

    std::vector<CatRow> catRows() const
    {
        std::vector<CatRow> rows{{kAll, "All", 0, false, false},
                                 {kInbox, "Inbox", 0, false, false}};
        addCatRows(0, 0, false, rows);
        // Rooms always show, so there's somewhere to create the first one.
        rows.push_back({kRooms, "Rooms", 0, !rooms_.empty(), false});
        if (!collapsed_.count(kRooms))
            for (const auto &r : rooms_)
                rows.push_back({roomView(r.id), r.name, 1, false, false});
        const auto archived = tree_.archiveRoots();
        if (archived.empty())
            return rows;
        rows.push_back({kArchive, "Archived", 0, true, false});
        if (collapsed_.count(kArchive))
            return rows;
        // Each archived category by its full path, with its subtree below.
        for (const auto *c : archived)
        {
            const bool kids = !tree_.children(c->id).empty();
            rows.push_back({c->id, tree_.path(c->id), 1, kids, true});
            if (kids && !collapsed_.count(c->id))
                addCatRows(c->id, 2, true, rows);
        }
        return rows;
    }

    // Outside the archive, archived categories are skipped.
    void addCatRows(int64_t parent, int depth, bool archived, std::vector<CatRow> &rows) const
    {
        auto shown = [&](int64_t id) {
            auto kids = tree_.children(id);
            if (!archived)
                kids.erase(std::remove_if(kids.begin(), kids.end(),
                                          [](const auto *c) { return c->archived; }),
                           kids.end());
            return kids;
        };
        for (const auto *c : shown(parent))
        {
            const bool kids = !shown(c->id).empty();
            rows.push_back({c->id, c->name, depth, kids, archived});
            if (kids && !collapsed_.count(c->id))
                addCatRows(c->id, depth + 1, archived, rows);
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
        for (const auto &t : status_ == Status::Pruned ? pruned_ : todos_)
        {
            if ((status_ == Status::Pending && t.done) || (status_ == Status::Done && !t.done))
                continue;
            if (!inView(t, catSelId_))
                continue;
            const bool idMatch = isId && t.id == qid;
            if (!q.empty() && !idMatch && lower(t.title).find(q) == std::string::npos)
                continue;
            v.push_back(&t);
        }
        std::stable_sort(v.begin(), v.end(),
                         [](const TodoItem *a, const TodoItem *b) { return todo::listOrder(*a, *b); });
        auto hit = std::find_if(v.begin(), v.end(), [&](auto *t) { return isId && t->id == qid; });
        if (hit != v.end())
            std::rotate(v.begin(), hit, hit + 1);
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
        // Hidden by a folded parent, or deleted: fall back to an ancestor,
        // or to the Archived row.
        for (int64_t id = catSelId_; idx < 0 && id > 0;)
        {
            const auto *c = tree_.find(id);
            id = c ? c->parentId : 0;
            idx = id > 0 ? findRow(id) : -1;
        }
        if (idx < 0 && catSelId_ > 0 && tree_.inArchive(catSelId_))
            idx = findRow(kArchive);
        if (idx < 0 && isRoomView(catSelId_))  // folded or deleted
            idx = findRow(kRooms);
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
        if (id > 0 && tree_.inArchive(id))
            collapsed_.erase(kArchive);
        if (isRoomView(id))
            collapsed_.erase(kRooms);
        catSelId_ = id;
        sel_ = 0;
        selId_ = 0;
        syncSelection();
        if (roomOf(catSelId_) != logRoom_)
            reload();  // shows the room's log
    }

    void moveCatSel(long delta)
    {
        auto rows = catRows();
        long s = std::clamp<long>(static_cast<long>(catSel_) + delta, 0, static_cast<long>(rows.size()) - 1);
        selectCategory(rows[static_cast<size_t>(s)].id);
    }

    void setStatus(int s)
    {
        status_ = static_cast<Status>((s + 4) % 4);
        syncSelection();
    }

    void startInput(Mode mode, const std::string &text)
    {
        input_.set(text);
        mode_ = mode;
        pendingFiles_.clear();
        offerPath_.clear();
        declined_.clear();
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
            case PickFor::Priority:
                for (int p = 3; p >= 0; --p)
                    pickOptions_.push_back(
                        {p, p ? todo::priorityMarker(p) + " " + todo::priorityName(p) : "none", 0});
                for (const auto &t : todos_)
                    if (t.id == subject)
                        current = t.priority;
                break;
            case PickFor::Theme:
            case PickFor::File: break;
        }
        for (const auto &[c, depth] : tree_.flatten(0, true))
        {
            if (purpose == PickFor::Priority)
                break;
            if (purpose == PickFor::MoveCategory && tree_.isWithin(c->id, subject))
                continue;  // can't move a category inside itself
            pickOptions_.push_back({c->id, c->name, depth});
        }
        if (purpose == PickFor::ShowCategory && !tree_.archiveRoots().empty())
            pickOptions_.push_back({kArchive, "Archived", 0});
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
        if (keys_.is(k, "pick_cancel"))
        {
            mode_ = Mode::Normal;
            if (pickFor_ == PickFor::Theme)
                setTheme(themeBefore_);
        }
        else if (keys_.is(k, "down"))
            move(1);
        else if (keys_.is(k, "up"))
            move(-1);
        else if (keys_.is(k, "top"))
            move(-n);
        else if (keys_.is(k, "bottom"))
            move(n);
        else if (keys_.is(k, "page_down"))
            move(std::max<long>(n / 2, 1));
        else if (keys_.is(k, "page_up"))
            move(-std::max<long>(n / 2, 1));
        else if (keys_.is(k, "pick_choose"))
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
                case PickFor::Priority:
                    act([&] {
                        api_.setExtras(subject, static_cast<int>(id), std::nullopt);
                        flash(id ? "Priority " + todo::priorityName(static_cast<int>(id))
                                 : std::string("Priority cleared"),
                              false);
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
                case PickFor::File:
                    if (static_cast<size_t>(id) < pickFiles_.size())
                        openFromList(pickFiles_, static_cast<size_t>(id));
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
            case Mode::Help:
                // The list scrolls; any other key closes it.
                if (keys_.is(k, "down") || keys_.is(k, "page_down"))
                    helpOffset_ += keys_.is(k, "down") ? 1 : 10;
                else if (keys_.is(k, "up") || keys_.is(k, "page_up"))
                    helpOffset_ = std::max(0, helpOffset_ - (keys_.is(k, "up") ? 1 : 10));
                else
                    mode_ = Mode::Normal;
                break;
            case Mode::AddTodo:
            case Mode::EditTodo:
            case Mode::AddCategory:
            case Mode::RenameCategory:
            case Mode::StyleCategory:
            case Mode::SetDeadline:
            case Mode::AddRoom:
            case Mode::RenameRoom:
            case Mode::RoomTopic:
                if (!handleMention(k))
                    handleInput(k);
                break;
            case Mode::PostMessage:
                if (!offerPath_.empty())
                    handleOffer(k);
                else if (!handleMention(k))
                    handleInput(k);
                break;
            case Mode::ViewFile: handleView(k); break;
            case Mode::Search: handleSearch(k); break;
            case Mode::Pick: handlePick(k); break;
            case Mode::ConfirmDeleteTodo:
            case Mode::ConfirmDeleteCategory:
            case Mode::ConfirmDeleteRoom:
            case Mode::ConfirmPruneLog:
            case Mode::ConfirmClear: handleConfirm(k); break;
        }
    }

    void handleNormal(const Key &k)
    {
        auto on = [&](const char *action) { return keys_.is(k, action); };
        static const Focus order[] = {Focus::Categories, Focus::Todos, Focus::Details};
        auto cycle = [&](int step) {
            int i = static_cast<int>(focus_);
            do
                i = (i + step + 3) % 3;
            while (!wide_ && order[i] != Focus::Todos);
            focus_ = order[i];
        };

        // The arrows follow the layout: details sits under the todos, so
        // moving down off the last todo goes there, and up off its top
        // comes back. ← / → reach the side panel and the todos.
        const bool lastTodo = sel_ + 1 >= visible().size();
        if (wide_ && focus_ == Focus::Todos && on("down") && lastTodo)
            focus_ = Focus::Details;
        else if (focus_ == Focus::Details && on("up") && detailsOffset_ == 0)
            focus_ = Focus::Todos;
        // Moving through a list comes first: in details it scrolls.
        else if (focus_ == Focus::Details && scrollDetails(k))
            ;
        else if (on("quit"))
            quit_ = true;
        else if (on("help"))
        {
            helpOffset_ = 0;
            mode_ = Mode::Help;
        }
        else if (on("focus_next"))
            cycle(1);
        else if (on("focus_prev"))
            cycle(-1);
        else if (on("focus_categories"))
            focus_ = Focus::Categories;
        else if (on("focus_todos"))
            focus_ = Focus::Todos;
        else if (on("focus_details"))
            focus_ = Focus::Details;
        else if (on("resize_left") || on("resize_right") || on("resize_up") || on("resize_down"))
            resize(on("resize_left") ? -2 : on("resize_right") ? 2 : 0,
                   on("resize_up") ? 1 : on("resize_down") ? -1 : 0);
        else if (on("reset_layout"))
        {
            leftW_ = detailsH_ = -1;
            saveLayout();
            flash("Default panel sizes", false);
        }
        else if (on("filter_prev"))
            setStatus(static_cast<int>(status_) - 1);
        else if (on("filter_next"))
            setStatus(static_cast<int>(status_) + 1);
        else if (on("hide_done"))
        {
            const bool hide = status_ != Status::Pending;
            setStatus(static_cast<int>(hide ? Status::Pending : Status::All));
            flash(hide ? "Hiding completed" : "Showing completed", false);
        }
        else if (on("new_todo"))
            startInput(Mode::AddTodo, "");
        else if (on("jump_category"))
            openPicker(PickFor::ShowCategory, 0);
        else if (on("prune_done"))
        {
            if (std::any_of(todos_.begin(), todos_.end(),
                            [&](auto &t) { return doneOutsideArchive(t); }))
                mode_ = Mode::ConfirmClear;
            else
                flash("No done todos to prune", false);
        }
        else if (on("search"))
            mode_ = Mode::Search;
        else if (on("clear_search") && !search_.text().empty())
        {
            search_.set("");
            syncSelection();
        }
        else if (on("refresh"))
        {
            reload();
            setTheme(theme_.name);  // pick up edits to the theme file
            if (loadKeys())         // and to the keys file
                flash("Refreshed", false);
        }
        else if (on("theme"))
            openThemePicker();
        else if (on("room_say"))
        {
            // The room being viewed, else the selected todo's.
            const TodoItem *cur = selected();
            sayRoom_ = targetRoom() ? targetRoom() : cur ? cur->roomId : 0;
            if (!sayRoom_)
                return flash("Select a room (under Rooms) to post in", true);
            if (api_.agent().empty())
                return flash("Say who you are first: todo --as NAME, or $TODO_AGENT", true);
            startInput(Mode::PostMessage, "");
        }
        else if (on("open_file"))
            openFileKey();
        else if (focus_ == Focus::Categories && wide_)
            handleCategoryKeys(k);
        else
            handleTodoKeys(k);  // details acts on the selected todo too

        if (!wide_)
            focus_ = Focus::Todos;  // the other panels are hidden; use jump_category instead
    }

    void handleCategoryKeys(const Key &k)
    {
        auto on = [&](const char *action) { return keys_.is(k, action); };
        const CategoryItem *cat = tree_.find(catSelId_);
        auto needCategory = [&] {
            flash("Select a category first (not All or Inbox)", true);
        };
        if ((catSelId_ == kRooms || isRoomView(catSelId_)) && handleRoomKeys(k))
            return;

        if (on("down"))
            moveCatSel(1);
        else if (on("up"))
            moveCatSel(-1);
        else if (on("top"))
            moveCatSel(-1'000'000);
        else if (on("bottom"))
            moveCatSel(1'000'000);
        else if (on("page_down"))
            moveCatSel(std::max(catRowsShown_ / 2, 1));
        else if (on("page_up"))
            moveCatSel(-std::max(catRowsShown_ / 2, 1));
        else if (on("cat_open"))
            focus_ = Focus::Todos;
        else if (on("cat_fold"))
        {
            auto rows = catRows();
            if (catSel_ < rows.size() && rows[catSel_].hasChildren)
            {
                if (!collapsed_.erase(catSelId_))
                    collapsed_.insert(catSelId_);
            }
        }
        else if (on("cat_add"))
        {
            inputParent_ = target();
            startInput(Mode::AddCategory, "");
        }
        else if (on("cat_rename"))
            cat ? startInput(Mode::RenameCategory, cat->name) : needCategory();
        else if (on("cat_move"))
            cat ? openPicker(PickFor::MoveCategory, cat->id) : needCategory();
        else if (on("cat_delete"))
            cat ? void(mode_ = Mode::ConfirmDeleteCategory) : needCategory();
        else if (on("cat_archive"))
            cat ? toggleArchived(*cat) : needCategory();
        else if (on("cat_style"))
        {
            if (!cat)
                return needCategory();
            startInput(Mode::StyleCategory, cat->style.empty() ? "" : cat->style.describe());
        }
    }

    // The category keys on the Rooms rows: add, rename, delete, topic and prune.
    // Returns false for keys that work as usual there (moving, folding...).
    bool handleRoomKeys(const Key &k)
    {
        auto on = [&](const char *action) { return keys_.is(k, action); };
        const RoomItem *room = findRoom(roomOf(catSelId_));
        auto needRoom = [&] { flash("Select a room first", true); };
        if (on("cat_add"))
            startInput(Mode::AddRoom, "");
        else if (on("cat_rename"))
            room ? startInput(Mode::RenameRoom, room->name) : needRoom();
        else if (on("cat_style"))
            room ? startInput(Mode::RoomTopic, room->topic) : needRoom();
        else if (on("cat_delete"))
            room ? void(mode_ = Mode::ConfirmDeleteRoom) : needRoom();
        else if (on("room_prune"))
            room ? void(mode_ = Mode::ConfirmPruneLog) : needRoom();
        else if (on("cat_move") || on("cat_archive"))
            flash("Rooms can't be moved or archived", true);
        else
            return false;
        return true;
    }

    void toggleArchived(const CategoryItem &cat)
    {
        const int64_t id = cat.id;
        const std::string name = tree_.path(id);
        if (!cat.archived && tree_.inArchive(id))
        {
            flash(name + " is inside an archived category; unarchive that instead", true);
            return;
        }
        const bool archive = !cat.archived;
        if (!act([&] { api_.setCategoryArchived(id, archive); }))
            return;
        flash((archive ? "Archived " : "Unarchived ") + name, false);
        selectCategory(id);  // it moved to / from the Archived section
    }

    void handleTodoKeys(const Key &k)
    {
        auto on = [&](const char *action) { return keys_.is(k, action); };
        const TodoItem *cur = selected();
        if (on("down"))
            moveSel(1);
        else if (on("up"))
            moveSel(-1);
        else if (on("top"))
            moveSel(-1'000'000);
        else if (on("bottom"))
            moveSel(1'000'000);
        else if (on("page_down"))
            moveSel(std::max(rows_ / 2, 1));
        else if (on("page_up"))
            moveSel(-std::max(rows_ / 2, 1));
        else if (on("todo_add"))
            startInput(Mode::AddTodo, "");
        else if (!cur)
            return;
        else if (cur->pruned())
            handlePrunedKeys(k, *cur);
        else if (on("todo_prune"))
        {
            if (!cur->done)
                return flash("Only done todos can be pruned", true);
            const int64_t id = cur->id;
            act([&] {
                auto p = api_.prune(id);
                flash("Pruned #" + std::to_string(id) + " → " + p.idLabel() + "; #" +
                          std::to_string(id) + " is free again",
                      false);
            });
        }
        else if (on("todo_toggle"))
        {
            int64_t id = cur->id;
            bool done = !cur->done;
            act([&] { api_.setDone(id, done); });
        }
        else if (on("todo_edit"))
        {
            editId_ = cur->id;
            startInput(Mode::EditTodo, cur->title);
        }
        else if (on("todo_describe"))
            editDescription(*cur);
        else if (on("todo_move"))
            openPicker(PickFor::MoveTodo, cur->id);
        else if (on("todo_priority"))
            openPicker(PickFor::Priority, cur->id);
        else if (on("todo_deadline"))
        {
            editId_ = cur->id;
            startInput(Mode::SetDeadline, cur->deadline);
        }
        else if (on("todo_delete"))
        {
            editId_ = cur->id;
            mode_ = Mode::ConfirmDeleteTodo;
        }
        else if (on("todo_claim") || on("todo_take"))
            claimOrRelease(*cur, on("todo_take"));
    }

    // Claims the todo for us, or releases our claim. Someone else's claim is
    // only taken over with `take`.
    void claimOrRelease(const TodoItem &t, bool take)
    {
        const std::string &me = api_.agent();
        if (me.empty())
            return flash("Say who you are first: todo --as NAME, or $TODO_AGENT", true);
        const int64_t id = t.id;
        if (t.claimedBy == me)
        {
            if (take)
                return flash("#" + std::to_string(id) + " is already yours", false);
            act([&] {
                api_.release(id);
                flash("Released #" + std::to_string(id), false);
            });
            return;
        }
        if (!t.claimedBy.empty() && !take)
            return flash("Claimed by " + t.claimedBy + "; " + keys_.labels("todo_take", 1) +
                             " takes it over",
                         true);
        const std::string from = t.claimedBy;
        act([&] {
            api_.claim(id, take);
            flash("Claimed #" + std::to_string(id) + (from.empty() ? "" : " from " + from), false);
        });
    }

    // In the Pruned view: restore or delete for good; nothing else applies.
    void handlePrunedKeys(const Key &k, const TodoItem &cur)
    {
        auto on = [&](const char *action) { return keys_.is(k, action); };
        if (on("todo_toggle") || on("todo_restore"))
        {
            const int64_t id = cur.id;
            act([&] {
                auto t = api_.restore(id);
                flash("Restored p" + std::to_string(id) + " as #" + std::to_string(t.id), false);
            });
        }
        else if (on("todo_delete"))
        {
            editId_ = cur.id;
            mode_ = Mode::ConfirmDeleteTodo;
        }
        else if (on("todo_edit") || on("todo_move") || on("todo_priority") || on("todo_deadline") ||
                 on("todo_prune") || on("todo_describe") || on("todo_claim") || on("todo_take"))
            flash("It's pruned: restore it first (" + keys_.labels("todo_restore", 1) + ")", true);
    }

    // Opens the todo's description in $VISUAL / $EDITOR (else nano or vi) and
    // saves what comes back; an empty file clears it.
    void editDescription(const TodoItem &t)
    {
        char path[] = "/tmp/todo-description-XXXXXX";
        const int fd = mkstemp(path);
        if (fd < 0)
            return flash("Couldn't create a temporary file for the editor", true);
        const std::string before = t.description.empty() ? "" : t.description + "\n";
        [[maybe_unused]] auto n = ::write(fd, before.data(), before.size());
        ::close(fd);

        std::string editor;
        for (const char *var : {"VISUAL", "EDITOR"})
            if (const char *e = std::getenv(var); e && *e && editor.empty())
                editor = e;
        if (editor.empty())
            editor = std::system("command -v nano >/dev/null 2>&1") == 0 ? "nano" : "vi";

        term_->suspend();
        const int rc = std::system((editor + " '" + path + "'").c_str());
        term_->resume();
        lastFrame_.clear();  // redraw everything
        term_->write("\033[2J");

        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        ::unlink(path);
        std::string after = ss.str();
        after.erase(after.find_last_not_of(" \t\r\n") + 1);
        if (rc != 0)
            return flash("The editor (" + editor + ") failed; description not changed", true);
        if (after == t.description)
            return flash("Description unchanged", false);
        const int64_t id = t.id;
        act([&] {
            api_.setDescription(id, after);
            flash(after.empty() ? "Description cleared" : "Description saved", false);
        });
    }

    // ── keys and layout ──

    // (Re)reads the key bindings; returns false if the file had problems
    // (they're shown).
    bool loadKeys()
    {
        std::vector<std::string> warnings;
        keys_ = Keymap::load(warnings);
        if (warnings.empty())
            return true;
        auto more = warnings.size() - 1;
        flash(warnings.front() + (more ? " (+" + std::to_string(more) + " more)" : ""), true);
        return false;
    }

    // Panel sizes, kept in ~/.config/todo/layout; -1 means automatic.
    void loadLayout()
    {
        std::ifstream in(configDir() + "/layout");
        std::string line;
        while (in && std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const auto key = theme_detail::trim(line.substr(0, eq));
            const int v = std::atoi(line.c_str() + eq + 1);
            if (key == "left")
                leftW_ = v > 0 ? v : -1;
            else if (key == "details")
                detailsH_ = v > 0 ? v : -1;
        }
    }

    void saveLayout() const
    {
        std::error_code ec;
        std::filesystem::create_directories(configDir(), ec);
        std::ofstream out(configDir() + "/layout", std::ios::trunc);
        out << "left = " << leftW_ << "\ndetails = " << detailsH_ << "\n";
    }

    // Moves the side divider by `dx` columns and the details divider by `dy`
    // rows (up = details taller), within what fits on screen.
    void resize(int dx, int dy)
    {
        if (!wide_)
            return flash("Make the terminal wider to see the side panels", true);
        if (dx)
            leftW_ = std::clamp(lastLeft_ + dx, kMinLeft, std::max(kMinLeft, lastW_ - kMinMain));
        if (dy)
            detailsH_ = std::clamp(lastDetails_ + dy, kMinDetails,
                                   std::max(kMinDetails, lastMain_ - kMinTodos));
        saveLayout();
    }

    // In details, the moving keys scroll its text. Returns true if `k` was one.
    bool scrollDetails(const Key &k)
    {
        auto on = [&](const char *action) { return keys_.is(k, action); };
        const int page = std::max(detailsRows_ / 2, 1);
        const int d = on("down") ? 1 : on("up") ? -1 : on("page_down") ? page
                    : on("page_up") ? -page : on("top") ? -1'000'000 : on("bottom") ? 1'000'000 : 0;
        if (!d)
            return false;
        detailsOffset_ = std::max(0, detailsOffset_ + d);  // drawDetails keeps it in range
        if (d < 0)
            logFollow_ = false;  // reading back in a room's log
        return true;
    }

    // People to suggest for the "@name" being typed in a message: the
    // room's members matching it, most recently seen first, not you. Empty
    // unless the cursor is at the end of an "@..." word.
    std::vector<std::string> mentionMatches() const
    {
        const std::string word = input_.wordBeforeCursor();
        const RoomItem *room = findRoom(sayRoom_);
        if (!room || word.empty() || word[0] != '@')
            return {};
        auto lower = [](std::string s) {
            for (auto &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        const std::string prefix = lower(word.substr(1));
        auto members = room->members;
        std::stable_sort(members.begin(), members.end(),
                         [](const auto &a, const auto &b) { return a.second > b.second; });
        std::vector<std::string> out;
        for (const auto &[name, seen] : members)
            if (name != api_.agent() && lower(name).compare(0, prefix.size(), prefix) == 0)
                out.push_back(name);
        // Nothing left to complete once it's typed out in full.
        if (out.size() == 1 && lower(out[0]) == prefix)
            out.clear();
        return out;
    }

    // While suggestions show: ↑/↓ pick one, tab or enter puts it in. Returns
    // true if it used the key.
    bool handleMention(const Key &k)
    {
        const auto matches = mentionMatches();
        if (matches.empty())
        {
            mentionSel_ = 0;
            return false;
        }
        const int n = static_cast<int>(matches.size());
        mentionSel_ = std::clamp(mentionSel_, 0, n - 1);
        if (k.type == Key::Down || k.type == Key::BackTab || k.type == Key::Up)
            mentionSel_ = (mentionSel_ + (k.type == Key::Down ? 1 : -1) + n) % n;
        else if (k.type == Key::Tab || (k.type == Key::Enter && !k.mods))
        {
            input_.replaceWordBeforeCursor("@" + matches[mentionSel_] + " ");
            mentionSel_ = 0;
        }
        else
            return false;
        return true;
    }

    void handleInput(const Key &k)
    {
        if (k.type == Key::Esc)
        {
            mode_ = Mode::Normal;
            return;
        }
        // Ctrl+V, or a paste with no text (the terminal had only an image to
        // give): attach the image on the clipboard, if there is one.
        if (mode_ == Mode::PostMessage &&
            (k.ctrl('v') || (k.type == Key::Paste && trim(k.ch).empty())))
        {
            const std::string img = clipboardImage();
            if (img.empty())
                flash("No image on the clipboard", true);
            else if (static_cast<int64_t>(std::filesystem::file_size(img)) > attach::kMaxSize)
                flash("The clipboard image is over 10 MB", true);
            else
            {
                pendingFiles_.push_back(img);
                flash("Attached the clipboard image", false);
            }
            return;
        }
        if (k.type == Key::Paste)
        {
            // A pasted (or dropped) path to an image or PDF: offer to attach it.
            if (mode_ == Mode::PostMessage && offerFile(attach::pathFromText(k.ch), k.ch, ""))
                return;
            input_.paste(k.ch, mode_ == Mode::PostMessage);  // only messages keep line breaks
            return;
        }
        if (mode_ == Mode::PostMessage && k.type == Key::Backspace && input_.text().empty() &&
            !pendingFiles_.empty())
        {
            pendingFiles_.pop_back();  // backspace in an empty box drops the last file
            return;
        }
        if (k.type == Key::Enter && k.mods && mode_ == Mode::PostMessage)
        {
            input_.newline();  // Shift / Alt / Ctrl+Enter: a new line in the message
            return;
        }
        if (k.type != Key::Enter)
        {
            input_.handle(k);
            return;
        }
        // A typed path to an image or PDF: offer to attach it before posting.
        if (mode_ == Mode::PostMessage && offerTyped())
            return;
        std::string text = trim(input_.text());
        // An empty deadline, style or topic clears it; a post can be just files.
        if (text.empty() && mode_ != Mode::SetDeadline && mode_ != Mode::StyleCategory &&
            mode_ != Mode::RoomTopic && !(mode_ == Mode::PostMessage && !pendingFiles_.empty()))
        {
            flash(mode_ == Mode::AddTodo || mode_ == Mode::EditTodo ? "Title can't be empty"
                  : mode_ == Mode::PostMessage                      ? "Nothing to post"
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
                const int64_t room = targetRoom();
                ok = act([&] {
                    selId_ = api_.add(text, cat, 0, "", "", room).id;
                    // Make sure the new todo is visible and selected.
                    if (status_ == Status::Done || status_ == Status::Pruned)
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
            case Mode::StyleCategory:
            {
                const int64_t id = catSelId_;
                CategoryStyle style;
                std::vector<std::string> words;
                for (size_t b = 0, e; b < text.size(); b = e + 1)
                {
                    e = text.find(' ', b);
                    if (e == std::string::npos)
                        e = text.size();
                    if (e > b)
                        words.push_back(lower(text.substr(b, e - b)));
                }
                if (auto err = style.apply(words); !err.empty())
                {
                    flash(err, true);
                    ok = false;
                    break;
                }
                ok = act([&] {
                    api_.setCategoryStyle(id, style);
                    flash(tree_.path(id) + ": " + style.describe(), false);
                });
                break;
            }
            case Mode::SetDeadline:
            {
                const int64_t id = editId_;
                auto deadline = todo::parseDeadline(text);
                if (!deadline)
                {
                    flash("Can't read \"" + text + "\": try 2026-10-31, today, tomorrow, +3 or fri",
                          true);
                    ok = false;
                    break;
                }
                ok = act([&] {
                    api_.setExtras(id, std::nullopt, *deadline);
                    flash(deadline->empty() ? "Deadline cleared"
                                            : "Due " + *deadline + " (" + todo::dueLabel(*deadline) + ")",
                          false);
                });
                break;
            }
            case Mode::AddRoom:
            {
                int64_t created = 0;
                ok = act([&] {
                    created = api_.addRoom(text, "").id;
                    flash("Created room " + text, false);
                });
                if (ok)
                    selectCategory(roomView(created));
                break;
            }
            case Mode::RenameRoom:
            case Mode::RoomTopic:
            {
                const int64_t room = roomOf(catSelId_);
                const bool rename = mode_ == Mode::RenameRoom;
                ok = act([&] {
                    if (rename)
                        api_.updateRoom(room, text, std::nullopt);
                    else
                        api_.updateRoom(room, std::nullopt, text);
                    flash(rename ? "Renamed to " + text : text.empty() ? "Topic cleared" : "Topic saved",
                          false);
                });
                break;
            }
            case Mode::PostMessage:
            {
                const int64_t room = sayRoom_;
                const auto paths = pendingFiles_;
                if (!paths.empty())
                    flash("Uploading " + std::to_string(paths.size()) + " file(s)...", false);
                ok = act([&] {
                    std::vector<AttachmentItem> files;
                    for (const auto &p : paths)
                        files.push_back(api_.upload(p));
                    api_.post(room, text, files);
                });
                logFollow_ = true;
                break;
            }
            default: break;
        }
        if (ok)  // on failure keep the popup open so the text can be fixed
            mode_ = Mode::Normal;
        syncSelection();
    }

    // ── attachments ──

    // Offers to attach the file at `path` if it's one the server takes (and
    // wasn't turned down already). `paste` is the pasted text, put in the box
    // if it's turned down; `token` is the typed word, taken out if it's
    // attached. True if it's offered.
    bool offerFile(const std::string &path, const std::string &paste, const std::string &token)
    {
        if (path.empty() || declined_.count(path) || attach::sniff(path).empty() ||
            std::find(pendingFiles_.begin(), pendingFiles_.end(), path) != pendingFiles_.end())
            return false;
        offerPath_ = path;
        offerPaste_ = paste;
        offerToken_ = token;
        return true;
    }

    // Looks for a path to a file in the message being posted: all of it,
    // else each word. True if one is offered.
    bool offerTyped()
    {
        const std::string text = trim(input_.text());
        if (offerFile(attach::pathFromText(text), "", text))
            return true;
        std::istringstream words(text);
        for (std::string w; words >> w;)
            if (offerFile(attach::pathFromText(w), "", w))
                return true;
        return false;
    }

    // y attaches the offered file, n leaves the text as it is.
    void handleOffer(const Key &k)
    {
        if (keys_.is(k, "confirm_yes"))
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(offerPath_, ec);
            const auto type = attach::sniff(offerPath_);
            if (ec || static_cast<int64_t>(size) > attach::maxFor(type))
            {
                flash(attach::baseName(offerPath_) + (ec ? " can't be read" : " is over " + attach::maxLabel(type)), true);
                declined_.insert(offerPath_);
            }
            else
            {
                pendingFiles_.push_back(offerPath_);
                if (!offerToken_.empty())  // the typed path goes; the file stands for it
                {
                    std::string text = input_.text();
                    if (auto at = text.find(offerToken_); at != std::string::npos)
                        text.erase(at, offerToken_.size());
                    for (size_t p; (p = text.find("  ")) != std::string::npos;)
                        text.erase(p, 1);
                    input_.set(trim(text));
                }
            }
        }
        else if (keys_.is(k, "confirm_no"))
        {
            declined_.insert(offerPath_);
            if (!offerPaste_.empty())
                input_.paste(offerPaste_, true);
        }
        else
            return;
        offerPath_.clear();
    }

    // What the open_file key shows: in a room, the files of the messages in
    // view (newest first), then the rest of the room's, so a file scrolled
    // out of view can still be opened; else the selected todo's.
    std::vector<AttachmentItem> filesInView() const
    {
        if (!isRoomView(catSelId_))
        {
            const TodoItem *t = selected();
            return t ? t->attachments : std::vector<AttachmentItem>{};
        }
        std::vector<AttachmentItem> all = logFiles_;
        for (auto m = log_.rbegin(); m != log_.rend(); ++m)
            for (const auto &a : m->attachments)
                if (std::none_of(all.begin(), all.end(), [&](const AttachmentItem &b) { return b.id == a.id; }))
                    all.push_back(a);
        return all;
    }

    // One file opens right away; with more, a list to pick from.
    void openFileKey()
    {
        pickFiles_ = filesInView();
        if (pickFiles_.empty())
            return flash(isRoomView(catSelId_) ? "No attachments in this room"
                                               : "No attachments on this todo",
                         true);
        if (pickFiles_.size() == 1)
            return openFromList(pickFiles_, 0);
        pickFor_ = PickFor::File;
        pickOptions_.clear();
        for (size_t i = 0; i < pickFiles_.size(); ++i)
            pickOptions_.push_back({static_cast<int64_t>(i), pickFiles_[i].marker(), 0});
        pickSel_ = 0;
        pickOffset_ = 0;
        mode_ = Mode::Pick;
    }

    // Downloads `a` (once, into the cache) and shows it: inline in kitty for
    // images, videos in mpv if it's there, else with xdg-open.
    void openAttachment(const AttachmentItem &a)
    {
        namespace fs = std::filesystem;
        const std::string base = attach::baseName(a.name);
        const fs::path dir = fileCacheDir() / a.id;
        const fs::path path = dir / (base.empty() || base == "." || base == ".." ? a.label() : base);
        std::error_code ec;
        if (!fs::exists(path, ec))
        {
            std::string data;
            try
            {
                data = api_.download(a.id);
            }
            catch (const ApiError &e)
            {
                online_ = !e.offline;
                return flash(e.what(), true);
            }
            fs::create_directories(dir, ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!out)
                return flash("Couldn't save " + path.string(), true);
        }
        if (a.image() && inKitty() && showInline(a, path))
            return;
        const char *opener = a.video() && onPath("mpv") ? "mpv" : "xdg-open";
        openExternally(path.string(), opener);
        flash("Opening " + a.name + " (" + opener + ")", false);
    }

    // Shows image `a`, saved at `path`, over the UI. Kitty takes PNG, so
    // other images are converted with ImageMagick first; false if that fails.
    bool showInline(const AttachmentItem &a, const std::filesystem::path &path)
    {
        std::filesystem::path png = path;
        std::error_code ec;
        if (a.type != "image/png")
        {
            png = path.parent_path() / "view.png";
            const std::string from = shellQuote(path.string() + "[0]"), to = shellQuote("png:" + png.string());
            if (!std::filesystem::exists(png, ec) &&
                std::system(("(magick " + from + " " + to + " || convert " + from + " " + to +
                             ") >/dev/null 2>&1")
                                .c_str()) != 0)
                return false;
        }
        std::ifstream in(png, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto [w, h] = pngSize(data);
        if (!w || !h)
            return false;
        viewFile_ = a;
        viewPng_ = std::move(data);
        viewW_ = w;
        viewH_ = h;
        viewSent_ = viewPlaced_ = false;
        mode_ = Mode::ViewFile;
        return true;
    }

    static constexpr int kImageId = 7317;  // our image's id in kitty

    // Puts the image in the middle of viewBox_, as big as fits (never
    // enlarged), sending it to the terminal the first time.
    void placeImage(Terminal &term)
    {
        winsize ws{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        // Cell size in pixels, guessed when the terminal doesn't say.
        const double cw = ws.ws_xpixel && ws.ws_col ? static_cast<double>(ws.ws_xpixel) / ws.ws_col : 8;
        const double ch = ws.ws_ypixel && ws.ws_row ? static_cast<double>(ws.ws_ypixel) / ws.ws_row : 16;
        const auto [bx, by, bw, bh] = viewBox_;
        if (bw <= 0 || bh <= 0)
            return;
        const double scale = std::min({bw * cw / viewW_, bh * ch / viewH_, 1.0});
        const int cols = std::clamp(static_cast<int>(std::ceil(viewW_ * scale / cw)), 1, bw);
        const int rows = std::clamp(static_cast<int>(std::ceil(viewH_ * scale / ch)), 1, bh);
        const std::string id = std::to_string(kImageId);
        // q=2: no replies, which would arrive as keys.
        std::string out = "\033_Ga=d,d=i,i=" + id + ",q=2\033\\";
        out += "\033[" + std::to_string(by + (bh - rows) / 2 + 1) + ";" +
               std::to_string(bx + (bw - cols) / 2 + 1) + "H";
        const std::string place = "i=" + id + ",c=" + std::to_string(cols) + ",r=" +
                                  std::to_string(rows) + ",C=1,q=2";
        if (!viewSent_)
        {
            // Sent base64-encoded, in chunks of 4096.
            const std::string b64 = drogon::utils::base64Encode(viewPng_);
            for (size_t off = 0; off < b64.size(); off += 4096)
                out += "\033_G" + (off ? std::string("q=2,") : "a=T,f=100," + place + ",") +
                       "m=" + (off + 4096 < b64.size() ? "1" : "0") + ";" + b64.substr(off, 4096) +
                       "\033\\";
            viewSent_ = true;
        }
        else
            out += "\033_Ga=p," + place + "\033\\";
        term.write(out);
        viewPlaced_ = true;
    }

    // Opens files[i], remembering the images around it so the viewer can
    // step through them.
    void openFromList(const std::vector<AttachmentItem> &files, size_t i)
    {
        viewList_.clear();
        viewAt_ = 0;
        for (size_t j = 0; j < files.size(); ++j)
            if (files[j].image())
            {
                if (j == i)
                    viewAt_ = viewList_.size();
                viewList_.push_back(files[j]);
            }
        openAttachment(files[i]);
    }

    // In the image viewer: up steps to the older image, down to the newer
    // one (the list runs newest first, as the log reads upwards); any other
    // key closes it.
    void handleView(const Key &k)
    {
        const bool up = k.type == Key::Up, down = k.type == Key::Down;
        if (!up && !down)
            return closeView();
        const size_t next = up ? viewAt_ + 1 : viewAt_ - 1;
        if ((up && next >= viewList_.size()) || (down && viewAt_ == 0))
            return flash(up ? "No older image" : "No newer image", false);
        term_->write("\033_Ga=d,d=I,i=" + std::to_string(kImageId) + ",q=2\033\\");
        lastFrame_.clear();
        viewAt_ = next;
        mode_ = Mode::Normal;  // back to ViewFile if it can be shown inline
        openAttachment(viewList_[viewAt_]);
    }

    void closeView()
    {
        term_->write("\033_Ga=d,d=I,i=" + std::to_string(kImageId) + ",q=2\033\\");
        viewPng_.clear();
        mode_ = Mode::Normal;
        lastFrame_.clear();  // redraw what was under it
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
        if (keys_.is(k, "confirm_no"))
        {
            mode_ = Mode::Normal;
            return;
        }
        if (!keys_.is(k, "confirm_yes"))
            return;

        if (mode_ == Mode::ConfirmDeleteTodo && status_ == Status::Pruned)
        {
            const int64_t id = editId_;
            act([&] {
                api_.removePruned(id);
                flash("Deleted p" + std::to_string(id) + " for good", false);
            });
        }
        else if (mode_ == Mode::ConfirmDeleteTodo)
        {
            const int64_t id = editId_;
            act([&] {
                api_.remove(id);
                flash("Deleted #" + std::to_string(id), false);
            });
        }
        else if (mode_ == Mode::ConfirmDeleteRoom)
        {
            const int64_t room = roomOf(catSelId_);
            const std::string name = roomName(room);
            if (act([&] { api_.removeRoom(room); }))
            {
                flash("Deleted room " + name, false);
                selectCategory(kRooms);
            }
        }
        else if (mode_ == Mode::ConfirmPruneLog)
        {
            const int64_t room = roomOf(catSelId_);
            act([&] {
                const auto n = api_.pruneMessages(room, 0);
                flash("Pruned " + std::to_string(n) + " message(s); their ids are free again", false);
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
                auto n = api_.pruneDone();
                flash("Pruned " + std::to_string(n) + " done todo(s); their ids are free again", false);
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
        logFiles_.clear();     // drawRoomLog fills it
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
            // Saved sizes (see resize()), kept within what fits.
            const int left = !wide_ ? 0
                             : std::clamp(leftW_ > 0 ? leftW_ : std::clamp(W / 4, 24, 36), kMinLeft,
                                          std::max(kMinLeft, W - kMinMain));
            // A room's log gets more room than a todo's details.
            const int autoDetails = isRoomView(catSelId_) ? std::max(6, main * 2 / 5) : 6;
            const int details = !wide_ ? 0
                                : std::clamp(detailsH_ > 0 ? detailsH_ : autoDetails, kMinDetails,
                                             std::max(kMinDetails, main - kMinTodos));
            lastW_ = W;
            lastMain_ = main;
            lastLeft_ = left;
            lastDetails_ = details;

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
                case Mode::RenameCategory:
                case Mode::StyleCategory:
                case Mode::SetDeadline:
                case Mode::AddRoom:
                case Mode::RenameRoom:
                case Mode::RoomTopic: drawInputPopup(W, H, cx, cy); break;
                case Mode::PostMessage: drawPostPopup(W, H, cx, cy); break;
                case Mode::ConfirmDeleteTodo:
                case Mode::ConfirmDeleteCategory:
                case Mode::ConfirmDeleteRoom:
            case Mode::ConfirmPruneLog:
                case Mode::ConfirmClear: drawConfirmPopup(W, H); break;
                case Mode::Pick: drawPickPopup(W, H); break;
                case Mode::Help: drawHelpPopup(W, H); break;
                case Mode::ViewFile: drawViewPopup(W, H); break;
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
            viewPlaced_ = false;  // the image goes back on top
        }
        if (mode_ == Mode::ViewFile && !viewPlaced_)
            placeImage(term);
    }

    void drawStatus(int x, int y, int w, int h)
    {
        panel(x, y, w, h, "Status", false);
        const int32_t state = online_ ? theme_.success : theme_.error;
        int e = c_.text(x + 2, y + 1, "● ", Style(state));
        e = c_.text(e, y + 1, online_ ? "online" : "offline", Style(state, Default, Bold));
        if (!api_.agent().empty())  // who we are in rooms
            c_.text(e, y + 1, truncate(" as " + api_.agent(), x + w - 2 - e), muted(), x + w - 1);
        std::string host = api_.server();
        if (auto p = host.find("://"); p != std::string::npos)
            host = host.substr(p + 3);
        c_.text(x + 2, y + 2, truncate(host, w - 4), muted());
    }

    void drawCategories(int x, int y, int w, int h)
    {
        const bool active = focus_ == Focus::Categories;
        panel(x, y, w, h, panelTitle("focus_categories", "Categories"), active);
        auto rows = catRows();
        const int visibleRows = h - 2;
        catRowsShown_ = visibleRows;
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
            const bool pseudo = row.id <= 0;
            // The category's look; its background fills the whole row. A
            // selected row keeps the selection colors, but the font style shows.
            const Look look = pseudo ? Look{} : lookOf(row.id);
            const int32_t bg = sel ? (active ? theme_.selectionBg : theme_.selectionBgInactive) : look.bg;
            if (bg != Default)
                c_.fill(x + 1, line, w - 2, 1, Style(Default, bg));
            const Style dim = sel ? Style(theme_.selectionFg, bg) : look.dim(bg, muted(bg));

            const std::string marker = row.hasChildren
                                           ? (collapsed_.count(row.id) ? "▸ " : "▾ ")
                                           : "  ";
            int cx = c_.text(x + 2 + 2 * row.depth, line, marker, dim, maxX);
            auto count = openCount(row.id);
            auto n = count ? std::to_string(count) : "";
            const int countX = maxX - 1 - static_cast<int>(n.size());
            Style nameStyle(sel ? theme_.selectionFg : pseudo ? Default : theme_.category, bg,
                            (sel ? Bold : row.archived ? Dim : 0) | look.attrs);
            if (!sel && look.fg != Default)
                nameStyle.fg = look.fg;
            c_.text(cx, line, truncate(row.name, countX - 1 - cx), nameStyle, maxX);
            c_.text(countX, line, n, dim, maxX);
        }
    }

    void drawTodos(int x, int y, int w, int h)
    {
        const bool active = focus_ == Focus::Todos;
        auto v = visible();
        std::string title = panelTitle("focus_todos", viewName(catSelId_));
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
            idWidth = std::max(idWidth, static_cast<int>(t->idLabel().size()));

        const int maxX = x + w - 1;
        for (int r = 0; r < rows_ && offset_ + r < static_cast<int>(v.size()); ++r)
        {
            const size_t idx = static_cast<size_t>(offset_ + r);
            const TodoItem &t = *v[idx];
            const int row = y + 1 + r;
            const bool sel = idx == sel_;
            // Todos look like their category; its background fills the row.
            const Look look = lookOf(t.categoryId);
            const int32_t bg = sel ? (active ? theme_.selectionBg : theme_.selectionBgInactive) : look.bg;
            if (bg != Default)
                c_.fill(x + 1, row, w - 2, 1, Style(Default, bg));
            const Style dim = sel ? Style(theme_.selectionFg, bg) : look.dim(bg, muted(bg));

            int cx = c_.text(x + 2, row, t.pruned() ? "[-]" : t.done ? "[x]" : "[ ]",
                             t.pruned() ? dim : Style(t.done ? theme_.done : theme_.pending, bg, Bold),
                             maxX);
            auto id = t.idLabel();
            cx = c_.text(cx + 1 + idWidth - static_cast<int>(id.size()), row, id, dim, maxX);

            if (t.priority)
                cx = c_.text(cx + 2, row, todo::priorityMarker(t.priority),
                             sel      ? Style(theme_.selectionFg, bg, Bold)
                             : t.done ? dim
                                      : Style(priorityColor(t.priority), bg, Bold),
                             maxX) - 1;

            // The deadline at the far right, then where the todo lives when
            // that isn't the category being viewed.
            int right = maxX - 1;
            if (!t.deadline.empty())
            {
                const std::string due = todo::dueLabel(t.deadline);
                right -= textWidth(due);
                c_.text(right, row, due,
                        sel      ? Style(theme_.selectionFg, bg)
                        : todo::overdue(t) ? Style(theme_.error, bg, Bold)
                        : todo::dueToday(t) ? Style(theme_.pending, bg, Bold)
                                            : dim,
                        maxX);
                right -= 2;
            }
            // Who's working on it.
            if (!t.claimedBy.empty())
            {
                const std::string who = "@" + truncate(t.claimedBy, 16);
                right -= textWidth(who);
                c_.text(right, row, who,
                        sel      ? Style(theme_.selectionFg, bg, Bold)
                        : t.done ? dim
                        : t.claimedBy == api_.agent() ? Style(theme_.success, bg, Bold)
                                                       : Style(theme_.accent, bg),
                        maxX);
                right -= 2;
            }
            std::string tag;
            if (t.categoryId != 0 && t.categoryId != catSelId_)
                tag = catSelId_ > 0 ? tree_.relativePath(t.categoryId, catSelId_)
                                    : tree_.path(t.categoryId);
            tag = truncate(tag, (w - 4) / 3);
            const int tagX = right - textWidth(tag);
            if (!tag.empty())
                c_.text(tagX, row, tag,
                        sel             ? Style(theme_.selectionFg, bg, look.attrs)
                        : look.plain()  ? Style(theme_.category, bg, Dim)
                                        : Style(look.fg == Default ? look.text : look.fg, bg, look.attrs),
                        maxX);

            const uint8_t strike = t.done ? Strike : 0;
            const int titleEnd = tag.empty() ? right : tagX - 2;
            Style titleStyle = sel      ? Style(theme_.selectionFg, bg, Bold | strike | look.attrs)
                               : t.done ? look.dim(bg, muted(bg))
                                        : Style(look.fg == Default ? look.text : look.fg, bg, look.attrs);
            if (t.done && !sel)
                titleStyle.attrs |= strike | look.attrs;
            // A ≡ after the title says there's a description, a + that there
            // are attachments (see details).
            const std::string marks = std::string(t.description.empty() ? "" : "≡") +
                                      (t.attachments.empty() ? "" : "+");
            const int mark = marks.empty() ? 0 : 1 + textWidth(marks);
            const int end = c_.text(cx + 2, row, truncate(t.title, titleEnd - (cx + 2) - mark),
                                    titleStyle, maxX);
            if (mark)
                c_.text(end + 1, row, marks, dim, maxX);
        }
    }

    // A category's look (see `todo cat style`), inherited from its parents,
    // ready to draw with. Default colors mean "as usual".
    struct Look
    {
        int32_t fg{Default}, bg{Default};
        int32_t text{Default};  // body text: readable on `bg` when there is one
        uint8_t attrs{0};

        bool plain() const { return fg == Default && bg == Default && !attrs; }
        // Secondary text: `usual` normally, dimmed body text on a background.
        Style dim(int32_t onBg, Style usual) const
        {
            return bg == Default ? usual : Style(text, onBg, Dim);
        }
    };

    Look lookOf(int64_t categoryId) const
    {
        const auto st = tree_.styleOf(categoryId);
        auto rgb = [](const std::string &hex) {
            return kRgb | static_cast<int32_t>(std::stol(hex.substr(1), nullptr, 16));
        };
        Look l;
        if (!st.fg.empty())
            l.fg = rgb(st.fg);
        if (!st.bg.empty())
        {
            l.bg = rgb(st.bg);
            // Near-black or near-white text, whichever reads better on it.
            const int32_t v = l.bg & 0xFFFFFF;
            const double lum = 0.299 * (v >> 16) + 0.587 * (v >> 8 & 255) + 0.114 * (v & 255);
            l.text = kRgb | (lum < 128 ? 0xF4F4F0 : 0x1E2126);
        }
        l.attrs = (st.bold ? Bold : 0) | (st.italic ? Italic : 0) | (st.underline ? Underline : 0);
        return l;
    }

    int32_t priorityColor(int p) const
    {
        return p == 3 ? theme_.error : p == 2 ? theme_.pending : theme_.category;
    }

    // "[1] Categories": the panel's name with the key that focuses it.
    std::string panelTitle(const char *focusAction, const std::string &name) const
    {
        const auto key = keys_.labels(focusAction, 1);
        return key.empty() ? name : "[" + key + "] " + name;
    }

    void drawDetails(int x, int y, int w, int h)
    {
        if (isRoomView(catSelId_))
            return drawRoomLog(x, y, w, h);
        const bool active = focus_ == Focus::Details;
        const TodoItem *t = selected();
        const int inner = w - 4;
        const int maxX = x + w - 1;
        // The title in bold, then the description (its own lines kept), then
        // the attachments.
        enum Kind { Plain, Title, File };
        std::vector<std::pair<std::string, Kind>> lines;
        if (t)
        {
            for (const auto &l : wrap(t->title, inner))
                lines.push_back({l, Title});
            if (!t->description.empty())
            {
                lines.push_back({"", Plain});
                std::istringstream desc(t->description);
                for (std::string para; std::getline(desc, para);)
                    for (const auto &l : wrap(para, inner))
                        lines.push_back({l, Plain});
            }
            if (!t->attachments.empty())
                lines.push_back({"", Plain});
            for (const auto &a : t->attachments)
                for (const auto &l : wrap(a.marker(), inner))
                    lines.push_back({l, File});
        }
        // The title scrolls under the info line; a new todo starts at the top.
        const int avail = std::max(h - 3, 0);
        detailsRows_ = avail;
        if (!t || t->id != detailsFor_)
            detailsOffset_ = 0;
        detailsFor_ = t ? t->id : 0;
        detailsOffset_ = std::clamp(detailsOffset_, 0, std::max(0, static_cast<int>(lines.size()) - avail));
        std::string footer;
        if (static_cast<int>(lines.size()) > avail && avail > 0)
            footer = std::to_string(detailsOffset_ + 1) + "-" +
                     std::to_string(std::min<int>(detailsOffset_ + avail, lines.size())) + " of " +
                     std::to_string(lines.size()) + " lines";
        panel(x, y, w, h, panelTitle("focus_details", "Details"), active, footer);
        if (!t)
        {
            c_.text(x + 2, y + 1, "No todo selected", muted(), maxX);
            return;
        }
        const Style dim = muted();
        int cx = c_.text(x + 2, y + 1, t->pruned() ? t->idLabel() : "#" + t->idLabel(),
                         Style(Default, Default, Bold));
        cx = c_.text(cx, y + 1, " · ", dim);
        cx = t->pruned() ? c_.text(cx, y + 1, "pruned " + formatTime(t->prunedAt), dim)
                         : c_.text(cx, y + 1, t->done ? "done" : "pending",
                                   Style(t->done ? theme_.done : theme_.pending));
        cx = c_.text(cx, y + 1, " · ", dim);
        cx = c_.text(cx, y + 1, viewName(t->categoryId), Style(theme_.category), maxX);
        if (t->priority)
        {
            cx = c_.text(cx, y + 1, " · ", dim);
            cx = c_.text(cx, y + 1, todo::priorityName(t->priority),
                         Style(priorityColor(t->priority)), maxX);
        }
        if (!t->deadline.empty())
        {
            cx = c_.text(cx, y + 1, " · ", dim);
            cx = c_.text(cx, y + 1, "due " + t->deadline + " (" + todo::dueLabel(t->deadline) + ")",
                         todo::overdue(*t) ? Style(theme_.error, Default, Bold) : Style(), maxX);
        }
        if (t->roomId)
        {
            cx = c_.text(cx, y + 1, " · ", dim);
            cx = c_.text(cx, y + 1, "room " + roomName(t->roomId), Style(theme_.accent), maxX);
        }
        if (!t->claimedBy.empty())
        {
            cx = c_.text(cx, y + 1, " · ", dim);
            cx = c_.text(cx, y + 1, "@" + t->claimedBy,
                         Style(t->claimedBy == api_.agent() ? theme_.success : theme_.accent, Default, Bold),
                         maxX);
        }
        c_.text(cx, y + 1, " · created " + formatTime(t->createdAt), dim, maxX);

        for (int i = 0; i < avail && detailsOffset_ + i < static_cast<int>(lines.size()); ++i)
        {
            const auto &[text, kind] = lines[detailsOffset_ + i];
            c_.text(x + 2, y + 2 + i, text,
                    kind == File ? Style(theme_.category) : Style(Default, Default, kind == Title ? Bold : 0),
                    maxX);
        }
    }

    // The room's topic and members, then its messages, newest at the bottom.
    // It follows new messages unless scrolled up.
    void drawRoomLog(int x, int y, int w, int h)
    {
        const bool active = focus_ == Focus::Details;
        const RoomItem *room = findRoom(roomOf(catSelId_));
        const int inner = w - 4;
        const int maxX = x + w - 1;

        struct Line
        {
            std::string time, author, text;  // time/author only on a message's first line
            bool event, mine;
            bool file;   // an attachment's [image: ...] marker
            size_t msg;  // which of log_ it's from
        };
        std::vector<Line> lines;
        for (size_t mi = 0; mi < log_.size(); ++mi)
        {
            const auto &m = log_[mi];
            const std::string time = rooms::clock(m.createdAt);
            const std::string who = m.author.empty() ? "server" : m.author;
            // Events read as a sentence: "alice claimed #3 ...".
            const std::string head = time + " " + who + (m.event ? " " : ": ");
            const int indent = textWidth(time) + 1;
            // The text's lines, then one per attachment.
            std::vector<std::pair<std::string, bool>> paras;
            std::istringstream text(m.text);
            for (std::string para; std::getline(text, para);)
                paras.push_back({para, false});
            for (const auto &a : m.attachments)
                paras.push_back({a.marker(), true});
            bool first = true;
            for (const auto &[para, file] : paras)
            {
                const int room = std::max(inner - (first ? textWidth(head) : indent), 8);
                for (const auto &l : wrap(para, room))
                {
                    lines.push_back({first ? time : std::string(indent - 1, ' '), first ? who : "", l,
                                     m.event, m.author == api_.agent(), file, mi});
                    first = false;
                }
            }
        }
        const int avail = std::max(h - 3, 0);
        detailsRows_ = avail;
        const int maxOffset = std::max(0, static_cast<int>(lines.size()) - avail);
        if (logFor_ != logRoom_)
            logFollow_ = true;
        logFor_ = logRoom_;
        if (logFollow_)
            detailsOffset_ = maxOffset;
        detailsOffset_ = std::clamp(detailsOffset_, 0, maxOffset);
        logFollow_ = detailsOffset_ == maxOffset;  // scrolling back down resumes following

        // The files of the messages in view, newest first, for open_file.
        std::set<size_t> shownMsgs;
        for (int i = 0; i < avail && detailsOffset_ + i < static_cast<int>(lines.size()); ++i)
            shownMsgs.insert(lines[detailsOffset_ + i].msg);
        for (auto mi = shownMsgs.rbegin(); mi != shownMsgs.rend(); ++mi)
            logFiles_.insert(logFiles_.end(), log_[*mi].attachments.begin(), log_[*mi].attachments.end());

        std::string footer = " " + keys_.labels("room_say", 1) + " post ";
        if (!logFiles_.empty() && !keys_.labels("open_file", 1).empty())
            footer += "· " + keys_.labels("open_file", 1) + " file" +
                      (logFiles_.size() > 1 ? "s (" + std::to_string(logFiles_.size()) + ") " : " ");
        if (detailsOffset_ < maxOffset)
            footer = " " + std::to_string(maxOffset - detailsOffset_) + " lines below ·" + footer;
        panel(x, y, w, h, panelTitle("focus_details", "Room " + (room ? room->name : "?")), active,
              footer);
        if (!room)
            return;

        // Topic, then who's around.
        int cx = c_.text(x + 2, y + 1, room->topic.empty() ? "no topic" : room->topic,
                         room->topic.empty() ? muted() : Style(Default, Default, Bold), maxX);
        for (size_t i = 0; i < room->members.size(); ++i)
        {
            const auto &[name, seen] = room->members[i];
            cx = c_.text(cx, y + 1, i ? ", " : "  · ", muted(), maxX);
            cx = c_.text(cx, y + 1, name,
                         Style(name == api_.agent() ? theme_.success : theme_.accent), maxX);
        }
        if (lines.empty())
        {
            c_.text(x + 2, y + 2, "No messages yet — press " + keys_.labels("room_say", 1) + " to post one",
                    muted(), maxX);
            return;
        }
        for (int i = 0; i < avail && detailsOffset_ + i < static_cast<int>(lines.size()); ++i)
        {
            const auto &l = lines[detailsOffset_ + i];
            const int row = y + 2 + i;
            int lx = c_.text(x + 2, row, l.time, muted(), maxX) + 1;
            if (l.event)
            {
                if (!l.author.empty())
                    lx = c_.text(lx, row, l.author + " ", muted(Default, Italic), maxX);
                c_.text(lx, row, l.text, muted(Default, Italic), maxX);
                continue;
            }
            if (!l.author.empty())
            {
                lx = c_.text(lx, row, l.author, Style(l.mine ? theme_.success : theme_.accent, Default, Bold),
                             maxX);
                lx = c_.text(lx, row, ": ", muted(), maxX);
            }
            if (l.file)
            {
                c_.text(lx, row, l.text, Style(theme_.category), maxX);
                continue;
            }
            // Mentions stand out: ours highlighted, others in the accent color.
            for (const auto &[part, kind] : rooms::splitMentions(l.text, api_.agent()))
                lx = c_.text(lx, row, part,
                             kind == rooms::Mention::Me
                                 ? Style(theme_.bg == Default ? 0 : theme_.bg, theme_.pending, Bold)
                             : kind == rooms::Mention::Other ? Style(theme_.accent, Default, Bold)
                                                             : Style(),
                             maxX);
        }
    }

    // Hints for the bottom bar: (keys, what they do), skipping unbound actions.
    using Hints = std::vector<std::pair<std::string, std::string>>;

    void keyHints(int y, int w, const Hints &hints)
    {
        int cx = 1;
        for (const auto &[key, desc] : hints)
        {
            if (key.empty())
                continue;
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
        auto key = [&](const char *action) { return keys_.labels(action, 1); };
        auto pair = [&](const char *a, const char *b) {
            auto x = key(a), y = key(b);
            return x.empty() || y.empty() ? x + y : x + " " + y;
        };
        if (focus_ == Focus::Categories && (catSelId_ == kRooms || isRoomView(catSelId_)))
            keyHints(y, w,
                     {{key("cat_add"), "new room"},
                      {key("cat_rename"), "rename"},
                      {key("cat_style"), "topic"},
                      {key("cat_delete"), "delete"},
                      {key("room_say"), "post"},
                      {filesInView().empty() ? "" : key("open_file"), "show file"},
                      {key("room_prune"), "prune log"},
                      {key("new_todo"), "new todo"},
                      {key("help"), "help"},
                      {key("quit"), "quit"}});
        else if (focus_ == Focus::Categories)
            keyHints(y, w,
                     {{key("cat_add"), "add subcategory"},
                      {key("cat_rename"), "rename"},
                      {key("cat_move"), "move"},
                      {key("cat_delete"), "delete"},
                      {key("cat_archive"), "archive"},
                      {key("cat_style"), "style"},
                      {key("cat_fold"), "fold"},
                      {pair("resize_left", "resize_right"), "resize"},
                      {key("new_todo"), "new todo"},
                      {key("help"), "help"},
                      {key("quit"), "quit"}});
        else if (focus_ == Focus::Details)
            keyHints(y, w,
                     {{pair("down", "up"), "scroll"},
                      {filesInView().empty() ? "" : key("open_file"), "show file"},
                      {pair("resize_up", "resize_down"), "taller / shorter"},
                      {pair("resize_left", "resize_right"), "side width"},
                      {key("reset_layout"), "reset sizes"},
                      {key("focus_todos"), "back to todos"},
                      {key("help"), "help"},
                      {key("quit"), "quit"}});
        else
            keyHints(y, w,
                     {{key("todo_add"), "add"},
                      {key("todo_edit"), "edit"},
                      {key("todo_toggle"), "toggle"},
                      {key("todo_claim"), "claim"},
                      {isRoomView(catSelId_) ? key("room_say") : "", "post"},
                      {filesInView().empty() ? "" : key("open_file"), "show file"},
                      {key("todo_move"), "move"},
                      {key("todo_priority"), "priority"},
                      {key("todo_deadline"), "deadline"},
                      {key("todo_delete"), "delete"},
                      {key("todo_prune"), "prune"},
                      {key("jump_category"), "category"},
                      {key("search"), "search"},
                      {key("hide_done"), "hide done"},
                      {pair("filter_prev", "filter_next"), "filter"},
                      {key("help"), "help"},
                      {key("quit"), "quit"}});
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
                title = target()       ? "New todo in " + tree_.path(target())
                        : targetRoom() ? "New todo in room " + roomName(targetRoom())
                                       : "New todo";
                break;
            case Mode::AddRoom: title = "New room"; break;
            case Mode::RenameRoom: title = "Rename room " + roomName(roomOf(catSelId_)); break;
            case Mode::RoomTopic: title = "Topic of " + roomName(roomOf(catSelId_)); break;
            case Mode::EditTodo: title = "Edit #" + std::to_string(editId_); break;
            case Mode::StyleCategory:
                title = "Style for " + tree_.path(catSelId_) + " (fg=red bg=blue bold italic)";
                break;
            case Mode::SetDeadline:
                title = "Deadline for #" + std::to_string(editId_) + " (2026-10-31, today, +3, fri)";
                break;
            case Mode::AddCategory:
                title = inputParent_ ? "New category in " + tree_.path(inputParent_)
                                     : "New category";
                break;
            default: title = "Rename " + tree_.path(catSelId_); break;
        }
        auto [x, y] = popup(W, H, w, 3, title,
                            mode_ == Mode::SetDeadline || mode_ == Mode::StyleCategory ||
                                    mode_ == Mode::RoomTopic
                                ? " empty clears · enter save · esc cancel "
                                : " enter save · esc cancel ");
        cx = input_.draw(c_, x + 2, y + 1, w - 4, Style());
        cy = y + 1;
    }

    // A post can run to several lines; the box grows to show up to 10. Under
    // the text: the files to attach, and the offer to attach one.
    void drawPostPopup(int W, int H, int &cx, int &cy)
    {
        const int w = std::min(W - 4, 72);
        const int extra = !pendingFiles_.empty() + !offerPath_.empty();
        const int rows = std::clamp(input_.rows(w - 4), 1, std::max(1, std::min(10, H - 4 - extra)));
        const std::string footer =
            !offerPath_.empty()     ? " " + keys_.labels("confirm_yes", 1) + " attach · " +
                                      keys_.labels("confirm_no", 1) + " leave it as text "
            : !pendingFiles_.empty() ? " enter post · backspace in an empty box drops a file · esc cancel "
                                     : " enter post · shift/alt+enter new line · ctrl+v image · esc cancel ";
        auto [x, y] = popup(W, H, w, rows + 2 + extra,
                            "Post to " + roomName(sayRoom_) + " as " + api_.agent(), footer);
        std::tie(cx, cy) = input_.drawWrapped(c_, x + 2, y + 1, w - 4, rows, Style());
        int line = y + 1 + rows;
        if (!pendingFiles_.empty())
        {
            std::string names;
            for (const auto &p : pendingFiles_)
                names += (names.empty() ? "" : ", ") + attach::baseName(p);
            c_.text(x + 2, line++, truncate("Attached: " + names, w - 4), Style(theme_.category), x + w - 1);
        }
        if (!offerPath_.empty())
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(offerPath_, ec);
            c_.text(x + 2, line, truncate("Attach " + attach::baseName(offerPath_) + " (" +
                                              attach::sniff(offerPath_) + ", " +
                                              sizeLabel(ec ? 0 : static_cast<int64_t>(size)) + ")?",
                                          w - 4),
                    Style(theme_.pending, Default, Bold), x + w - 1);
        }
        drawMentions(x, y + rows + 2 + extra, w, H);
    }

    // The "@name" suggestions, in a box under the input.
    void drawMentions(int x, int y, int w, int H)
    {
        const auto matches = mentionMatches();
        const int shown = std::min<int>(matches.size(), std::max(0, H - 2 - y - 1));
        if (shown <= 0)
            return;
        const int sel = std::clamp(mentionSel_, 0, static_cast<int>(matches.size()) - 1);
        const int first = std::clamp(sel - shown + 1, 0, static_cast<int>(matches.size()) - shown);
        c_.fill(x, y, w, shown + 2, Style());
        panel(x, y, w, shown + 2, "", true, " ↑↓ pick · tab/enter insert ");
        for (int i = 0; i < shown; ++i)
        {
            const bool on = first + i == sel;
            const Style st = on ? Style(theme_.selectionFg, theme_.selectionBg, Bold) : Style();
            if (on)
                c_.fill(x + 1, y + 1 + i, w - 2, 1, st);
            c_.text(x + 2, y + 1 + i, "@" + matches[first + i], st, x + w - 2);
        }
    }

    void drawConfirmPopup(int W, int H)
    {
        const int w = std::min(W - 4, 60);
        std::vector<std::string> lines;
        std::string title = "Delete";
        if (mode_ == Mode::ConfirmClear)
        {
            title = "Prune done todos";
            auto n = std::count_if(todos_.begin(), todos_.end(),
                                   [&](auto &t) { return doneOutsideArchive(t); });
            lines.push_back("Prune " + std::to_string(n) + " done todo(s)?");
            lines.push_back("Their ids are freed; they stay under Pruned ( ] ).");
            if (n != std::count_if(todos_.begin(), todos_.end(), [](auto &t) { return t.done; }))
                lines.push_back("Completed todos in archived categories are kept.");
        }
        else if (mode_ == Mode::ConfirmDeleteRoom)
        {
            title = "Delete room";
            lines.push_back("Delete room “" + truncate(roomName(roomOf(catSelId_)), w - 20) + "”?");
            lines.push_back("Its messages are deleted.");
            lines.push_back("Its todos stay, outside any room.");
        }
        else if (mode_ == Mode::ConfirmPruneLog)
        {
            const int64_t room = roomOf(catSelId_);
            title = "Prune messages";
            lines.push_back("Prune the messages of “" + truncate(roomName(room), w - 28) + "”?");
            lines.push_back("Their ids are freed, so new ones start small;");
            lines.push_back("they're kept: todo room pruned " + truncate(roomName(room), w - 30));
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
            const bool pruned = status_ == Status::Pruned;
            std::string name = (pruned ? "p" : "#") + std::to_string(editId_);
            for (const auto &t : pruned ? pruned_ : todos_)
                if (t.id == editId_)
                    name = t.title;
            lines.push_back("Delete “" + truncate(name, w - 16) + "”" + (pruned ? " for good?" : "?"));
        }
        auto [x, y] = popup(W, H, w, static_cast<int>(lines.size()) + 2, title,
                            " " + keys_.labels("confirm_yes", 1) + " confirm · " +
                                keys_.labels("confirm_no", 1) + " cancel ");
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
        else if (pickFor_ == PickFor::File)
            title = "Show which file";
        else if (pickFor_ == PickFor::MoveTodo)
            title = "Move #" + std::to_string(pickSubject_) + " to";
        else if (pickFor_ == PickFor::Priority)
            title = "Priority of #" + std::to_string(pickSubject_);
        else if (pickFor_ == PickFor::MoveCategory)
        {
            const auto *c = tree_.find(pickSubject_);
            title = "Move “" + (c ? c->name : std::string("?")) + "” into";
        }
        auto [x, y] = popup(W, H, w, h, title,
                            " " + keys_.labels("pick_choose", 1) + " choose · " +
                                keys_.labels("pick_cancel", 1) + " cancel ");
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
            const bool isCategory = pickFor_ != PickFor::Theme && pickFor_ != PickFor::Priority &&
                                    pickFor_ != PickFor::File && opt.id > 0;
            c_.text(x + 2 + 2 * opt.depth, line, opt.label,
                    Style(sel ? theme_.selectionFg : isCategory ? theme_.category : Default, bg,
                          sel ? Bold : 0),
                    x + w - 1);
        }
    }

    // A frame for the image (placeImage draws it inside).
    void drawViewPopup(int W, int H)
    {
        const std::string dims = std::to_string(viewW_) + "x" + std::to_string(viewH_);
        const int w = W - 2, h = H - 2;
        auto [x, y] = popup(W, H, w, h, viewFile_.name + " · " + dims + " · " + viewFile_.label(),
                            viewList_.size() > 1
                                ? " " + std::to_string(viewAt_ + 1) + "/" + std::to_string(viewList_.size()) +
                                      " · up older · down newer · any other key closes "
                                : " any key closes ");
        viewBox_ = {x + 2, y + 1, w - 4, h - 2};
    }

    // Every action with its keys, by group; scrolls when it doesn't fit.
    void drawHelpPopup(int W, int H)
    {
        std::vector<std::pair<std::string, std::string>> rows;  // ("", group) is a heading
        const char *group = "";
        for (const auto &a : actions())
        {
            if (std::string(group) != a.group)
            {
                if (!rows.empty())
                    rows.push_back({"", ""});
                rows.push_back({"", group = a.group});
            }
            const auto keys = keys_.labels(a.name, 3);
            rows.push_back({keys.empty() ? "(none)" : keys, a.help});
        }
        const int w = std::min(W - 4, 64);
        const int h = std::min(H - 2, static_cast<int>(rows.size()) + 2);
        const int shown = h - 2;
        helpOffset_ = std::clamp(helpOffset_, 0, std::max(0, static_cast<int>(rows.size()) - shown));
        const std::string footer = static_cast<int>(rows.size()) > shown
                                       ? " " + keys_.labels("down", 1) + "/" + keys_.labels("up", 1) +
                                             " scroll · any key closes · " + keysPath() + " "
                                       : " any key closes · " + keysPath() + " ";
        auto [x, y] = popup(W, H, w, h, "Keys", footer);
        for (int i = 0; i < shown && helpOffset_ + i < static_cast<int>(rows.size()); ++i)
        {
            const auto &[keys, text] = rows[helpOffset_ + i];
            const int row = y + 1 + i;
            if (keys.empty())
                c_.text(x + 2, row, text, Style(Default, Default, Bold), x + w - 1);
            else
            {
                c_.text(x + 4, row, truncate(keys, 20), Style(theme_.accent, Default, Bold), x + w - 1);
                c_.text(x + 26, row, text, Style(), x + w - 1);
            }
        }
    }

    Api &api_;
    Canvas c_;
    Theme theme_;
    Keymap keys_;
    Terminal *term_{nullptr};  // set while run() is going
    std::vector<std::string> themeNames_;  // theme picker options
    std::string themeBefore_;              // restored if the picker is cancelled
    std::string lastFrame_;
    bool wide_{true};

    std::vector<TodoItem> todos_;
    std::vector<TodoItem> pruned_;  // their ids are p1, p2..., apart from todos_'
    std::vector<RoomItem> rooms_;
    std::vector<MessageItem> log_;  // the viewed room's latest messages
    int64_t logRoom_{0};            // whose messages log_ holds
    int64_t logVersion_{0};         // that room's logVersion when log_ was loaded
    int64_t logFor_{0};             // the room the log was last drawn for
    bool logFollow_{true};          // keep the newest message in view
    int64_t sayRoom_{0};            // where PostMessage posts
    int mentionSel_{0};             // the highlighted "@name" suggestion
    std::vector<AttachmentItem> logFiles_;   // those of the messages drawn in the log
    std::vector<AttachmentItem> pickFiles_;  // what PickFor::File picks from
    std::vector<AttachmentItem> viewList_;  // the images the viewer steps through, newest first
    size_t viewAt_{0};                      // which of them is shown
    std::vector<std::string> pendingFiles_;  // paths to attach to the post
    std::string offerPath_;   // a file the post box offers to attach ("" for none)
    std::string offerPaste_;  // the paste it came from, put in if turned down
    std::string offerToken_;  // or the typed word, taken out if attached
    std::set<std::string> declined_;  // files not to offer again in this post
    AttachmentItem viewFile_;  // the image ViewFile shows
    std::string viewPng_;      // it as PNG, for kitty
    int viewW_{0}, viewH_{0};
    bool viewSent_{false}, viewPlaced_{false};
    std::tuple<int, int, int, int> viewBox_{0, 0, 0, 0};  // x, y, columns, rows for it
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

    // Layout: saved sizes (-1 = automatic) and what was last drawn.
    static constexpr int kMinLeft = 16, kMinMain = 30, kMinDetails = 4, kMinTodos = 5;
    int leftW_{-1}, detailsH_{-1};
    int lastW_{80}, lastMain_{24}, lastLeft_{24}, lastDetails_{6};
    int catRowsShown_{1};
    int detailsOffset_{0}, detailsRows_{1};
    int64_t detailsFor_{0};
    int helpOffset_{0};

    bool quit_{false};
};
}  // namespace

int runTui(Api &api)
{
    std::setlocale(LC_CTYPE, "");  // for wcwidth() on non-ASCII titles
    return App(api).run();
}
