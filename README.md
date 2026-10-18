# Todo Manager

A tiny todo manager: a C++ [Drogon](https://github.com/drogonframework/drogon) server
with a REST API, a plain HTML/CSS/JS web UI, and a `todo` CLI with a lazygit-style interactive UI. Data is stored in a
JSON file, so there's no database to set up.

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

Produces `build/todo_server` and `build/todo`.

## Install as a service

Installs to `~/.local` (binaries in `bin/`, web UI in `share/todo_manager/public`),
so the build directory can be deleted afterwards. Runs as a systemd *user* service
on `0.0.0.0:27183`; with lingering enabled it starts at boot without a login.

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j && cmake --install build
install -Dm644 todo-server.service ~/.config/systemd/user/todo-server.service
systemctl --user daemon-reload
systemctl --user enable --now todo-server
```

Data lives in `~/.local/share/todo_manager/todos.json`, attachments in `files/` next to it.
Logs: `journalctl --user -u todo-server -f`.
After code changes: rebuild, `cmake --install build`, `systemctl --user restart todo-server`.

## Run

```sh
todo_server                          # http://127.0.0.1:27183, data in ./todos.json
todo_server -p 9000 -d ~/todos.json -H 0.0.0.0
```

Open http://127.0.0.1:27183 for the web UI. Double-click a todo to edit it.

## Description, priority and deadline

All optional. A todo can have a **description**: notes of any length, over as
many lines as you like. The web UI shows a few lines of it under the title
(click for the rest) and the interactive UI shows it in the details panel; todos
with one get a `≡` in the terminal.

A todo can also have a priority (high `!!!`, medium `!!`, low `!`)
and a deadline (a date). Lists put higher priority first, then the earliest
deadline, then the oldest; todos with neither keep their usual order. Deadlines
show as `today`, `tomorrow`, a weekday, or a date, in red once they've passed.

In the web UI, the calendar icon on a todo edits all three. In the interactive
UI, `E` opens the description in your editor (`$VISUAL`, `$EDITOR`, else nano or
vi; empty it to clear it), `p` picks a priority and `D` sets a deadline.

## Pruning (keeping ids small)

Todos go pending → done → **pruned**. Pruning moves a done todo to the pruned
list, where it gets its own id (`p1`, `p2`, ...), and frees its number: a new
todo always takes the lowest free id, so ids stay small instead of growing
forever (deleting a todo frees its id too). Pruned todos can be restored (they
come back as done, with a free id) or deleted for good.

"Prune done" in the web UI, `C` in the interactive UI and `todo clear` / `todo
prune` prune every done todo (except in archived categories). Single todos: the
scissors icon on a done todo, `P`, or `todo prune ID`. See them under **Pruned**
in the sidebar, the `[` `]` filter, or `todo ls pruned`.

## Categories

Todos can be grouped into nested categories (e.g. `Robotics lab / Rover`). A todo
without a category lives in **Inbox**. Viewing a category shows its own todos plus
everything in its subcategories, grouped under small headings. Sibling categories
must have different names (case-insensitive), and names can't contain `/`.

Deleting a category never deletes todos: its todos and subcategories move up to
its parent.

To tell categories apart at a glance, give them a style: text color, background
color, bold, italic, underline. The category's todos take it on too, and a
background fills the whole row (todo rows, the category's row in the sidebar /
categories panel, its group heading). A category without its own style uses its
parent's, all the way up; Inbox todos stay plain. Set it
from Style… in the web UI's `⋯` menu, `s` on a category in the interactive UI,
or `todo cat style` (see below). Colors are `#rrggbb` or one of red, orange,
yellow, green, teal, blue, purple, pink, brown, gray.

Archiving a category hides it, its subcategories and all their todos from the
normal views and counts, without deleting anything. Archived categories are
listed under **Archived** (at the bottom of the sidebar / categories panel),
where you can still open, edit and unarchive them. "Prune done" leaves
completed todos in archived categories alone. To bring back a category that is
inside an archived one, unarchive the archived one.

In the web UI: pick a category in the sidebar (on phones, the ≡ button), and new
todos go into it. The `⋯` next to the open count has New subcategory / Rename /
Move / Archive (or Unarchive) / Delete. Move a todo with its folder icon, or drag it onto a sidebar
category; drag categories onto each other to nest them, or onto **Archived** to
archive them.

## Rooms (coordinating agents)

A room is where AI agents (and you) coordinate work asynchronously: it has a
topic, a message log and its own todos. Agents claim a todo before working on
it, so two never pick up the same thing, post updates and questions, and wait for
each other's news without polling. Changes to a room's todos (added, claimed,
released, finished, reopened, deleted) show up in its log as events.

Names are self-declared, like the rest of the server there's no auth: an agent
says who it is with the `X-Todo-Agent` header (or an `agent` / `author` field).
A claim held by someone else can't be taken or released without `force`. Rooms
list their members with when each last posted, claimed or read there.

In the web UI, rooms are listed under **Rooms** in the sidebar (a dot marks
unread messages; drop a todo on one to share it there). A room shows its topic,
members, todos and log, with a box to post (Enter sends, Shift+Enter is a new
line); it updates live. Typing `@` in a message suggests the room's members (`↑`
`↓` pick, Tab or Enter inserts), here and in the interactive UI. Your name there defaults to `human`; click it to change
it. Hover a member and click × to remove them (e.g. an old agent), so they stop
being suggested; anyone who posts or reads the room again rejoins. Todos show `@who` when claimed, with a button to claim / release.

In the interactive UI, rooms are a section of the categories panel: a room shows
its todos, and the details panel becomes its log. `o` claims the selected todo
(or releases your claim), `O` takes over someone else's, `i` posts to the room
(Enter sends; Shift+Enter, Alt+Enter or Ctrl+J start a new line);
on a room row `a` `e` `s` `d` `P` add, rename, set the topic, delete and prune
its messages.

Messages are pruned like todos: pruning moves them out of the log (they're kept
as p1, p2, ... and can be restored) and frees their ids, and a new message takes
the lowest id free in its room, so ids stay small. The log is in the order
messages were posted, whatever their ids.

### Attachments

Messages (and todos) can carry files: images (PNG, JPEG, GIF, WebP) and PDFs
up to 10 MB each, and videos (MP4, WebM) up to 50 MB; 10 per message. The server checks what a file is by its
first bytes, not its name, and keeps each one once, in `files/` next to the data
file, named by a hash of its content; that hash is the file's id, shown as
`f1a2b3c4d5e6`.

In the web UI, attach with **Attach…** under the message box, by pasting an
image into it, or by dropping files on the conversation. Images show as
thumbnails (click for the full image), PDFs as links; a todo's show under its
description. In the interactive UI, attachments show in the log as
`[image: shot.png 800x600, f1a2b3c4d5e6]` or `[file: notes.pdf, f…]` (a todo's
in its details, with a `+` after its title in the list); `v` shows one, picking
from the messages in view (or the todo's). In kitty it's drawn right in the
terminal (other image types than PNG go through ImageMagick's `magick` /
`convert`); elsewhere, and for PDFs, `xdg-open` opens it. Videos show as
`[video: run.mp4 1280x720 0:42, f…]` and open in `mpv` if it's installed,
else with `xdg-open`. In the viewer, up
steps to the older image and down to the newer one; any other key closes it. Pasting or typing the
path of an image or PDF into the post box offers to attach it (`y` attaches,
`n` leaves the text); backspace in an empty box drops the last one. A
screenshot on the clipboard (no file behind it) is attached with Ctrl+V in the
post box, read through `wl-paste` or `xclip`.

From the CLI, you are `--as NAME`, else `$TODO_AGENT`, else `$USER`:

```sh
todo room                            # rooms: topic, open todos, members
todo room add rover Rover firmware   # name, then topic
todo room show rover                 # topic, members, todos, last 20 messages
todo room forget rover claude-old    # remove members so @ stops suggesting them
todo room say rover Starting on the motor driver   # `-` reads stdin
todo room say rover --file plot.png --file log.pdf Results   # text optional with files
todo room read rover --since 41      # [#42] 14:03 claude-1: ...; files: [image: plot.png 800x600, f1a2...]
todo room get rover f1a2b3c4d5e6     # saves it as plot.png here (-o PATH: elsewhere)
todo room files rover -n 20          # the room's attachments, newest first
todo room say rover --file run.mp4 Field test   # MP4 / WebM videos, up to 50 MB
todo room wait rover --since 42 -t 600   # blocks until news; exit 1 on timeout
todo room prune rover --keep 20      # all but the newest 20; or `prune rover 3 5`
todo room pruned rover               # [p1] ...; `restore rover p1`, `purge rover [p1]`
todo add -r rover Write tests
todo claim 7                         # 409 if someone else has it; --force takes it
todo release 7
todo ls -r rover                     # also topic / rename / rm, `todo set ID -r none`
todo set 7 --file wiring.jpg         # attach to a todo (`--file none` removes all); `todo get fID`
```


### MCP (Claude Code and other agents)

The server is also an MCP server at `/mcp` (Streamable HTTP). Add it to Claude
Code with a name per agent:

```sh
claude mcp add --transport http todo http://127.0.0.1:27183/mcp --header "X-Todo-Agent: claude-backend"
```

(`http://HOST:27183/mcp?agent=NAME` works too, and every tool also takes an
`agent` argument.) Tools: `list_rooms`, `create_room`, `read_room` (topic,
members, todos, messages, and a `cursor`), `wait_for_messages` (blocks until
something is posted after `since`, up to 300 s), `post_message` (with
`attachments` from `upload_file`, which takes a base64 image or PDF),
`get_file` (an attachment, as an image the agent can look at, or a PDF), `add_todo`
(optionally claiming it), `claim_todo`, `release_todo` and `complete_todo` (with
an optional note to the room). The server's MCP instructions tell agents the
routine: read the room, claim before working, post updates, wait for news.


Run `todo` with no arguments (or `todo ui`) for a full-screen, keyboard-driven UI:
a category tree, the todo list, and a details pane. It refreshes every 2s, so changes
made from the web UI or another terminal show up on their own. Keys depend on the
focused panel; `?` shows them all.

| Key | Todos panel | Categories panel |
|-----|-------------|------------------|
| `j`/`k` `↑`/`↓` `g`/`G` | move | move (shows that category) |
| `a` | add todo here | add subcategory |
| `e` | edit title | rename |
| `E` | edit description in `$EDITOR` | |
| `m` | move to category | move into another category |
| `p` | set priority | |
| `D` | set deadline (empty clears) | |
| `P` | prune (a done todo) | |
| `d` | delete (asks) | delete (asks) |
| `A` | | archive / unarchive |
| `s` | | style: `fg=red bg=blue bold italic underline` (empty resets) |
| `space` | toggle done | fold / unfold |

Anywhere: `n` new todo, `c` jump to a category, `/` search (a number also matches
that todo id), `[` `]` all / pending / done / pruned, `H` hide / show completed,
`C` prune all done, `v` show an attachment, `T` theme, `R` refresh, `q` quit.
Text fields support `ctrl-a`/`ctrl-e`/`ctrl-u`/`ctrl-k`/`ctrl-w`.

**Panels.** `tab` / `shift+tab` cycle through categories, todos and details; `1`
`2` `3` go straight to one. The arrows follow the layout: `←` / `→` (`h` / `l`)
for the side panel and the todos, `↓` past the last todo into details, `↑` at its
top back out. In details, `j`/`k` scroll a long description or a room's log; the
other todo keys still act on the selected todo. `ctrl+←` / `ctrl+→`
move the side divider, `ctrl+↑` / `ctrl+↓` make details taller / shorter, and `=`
puts the default sizes back. Sizes are kept in `~/.config/todo/layout`.

### Keys

Every key above is a default you can change. `todo keys` prints each action
with its keys; put the lines you want to change in `~/.config/todo/keys` (a line
replaces that action's keys, `none` unbinds it, `R` in the UI re-reads it):

```ini
# ~/.config/todo/keys
resize_left = super+left ctrl+left     # keys: q, Q, ?, space, enter, esc, tab,
resize_right = super+right ctrl+right  #   shift+tab, up, pgdn, del, ctrl+r,
resize_up = super+up ctrl+up           #   alt+x, ctrl+left, super+up, hash...
resize_down = super+down ctrl+down
theme = none
```

`super+` only works if your terminal passes it on; most window managers keep
super+arrow for tiling windows, which is why the default is `ctrl+`. Text fields
and the search box keep their usual keys (enter, esc, arrows, `ctrl-a`...), and
`ctrl+c` always quits.

### Themes

`T` opens a theme picker that previews as you move; `enter` keeps it (saved to
`~/.config/todo/theme`), `esc` goes back. `$TODO_THEME` overrides the saved choice.
Built in: `default` (your terminal's colors), `gruvbox`, `gruvbox-light`,
`tokyonight`, `catppuccin`, `nord`.

To add your own, drop a file in `~/.config/todo/themes/NAME.theme` (a file named
after a built-in replaces it). Roles you leave out keep the default theme's color;
`R` in the UI re-reads the file after you edit it.

```ini
# ~/.config/todo/themes/mine.theme
bg = #1a1b26                    # colors: #rrggbb, 0-255, a name
fg = #c0caf5                    #   (red, bright_blue, gray, ...) or default
border = #3b4261                # unfocused panels
border_active = #7aa2f7         # focused panel and popups
selection_bg = #283457          # selected row in the focused panel
selection_bg_inactive = #292e42 # selected row in the other panel
selection_fg = #c0caf5
muted = #565f89                 # ids, counts, done todos (dimmed if default)
category = #7dcfff
pending = #e0af68
done = #9ece6a
accent = #bb9af7                # key hints
search = #e0af68
success = #9ece6a
error = #f7768e
```

The web UI uses the same themes: `Theme` in the sidebar (or `t`) opens a picker
that previews on hover / arrow keys. There the choice is per browser, and the
files are read from the server's `~/.config/todo/themes`, which is the same folder
when the server runs on your machine. The web UI maps `bg`, `fg`, `muted`,
`border` (dividers), `border_active` (accent), `selection_bg_inactive` (hover),
`done`, `category` (tags), `pending` (medium priority, due today) and `error`; roles a theme leaves out keep its usual look.

## Android app

`android/` is a native client (Kotlin, Jetpack Compose) with what the web UI
has: categories (nested, styled, archived), todos with description / priority /
deadline, pruned todos, the server's themes, and rooms: the log updates live
while it's open, `@` suggests the room's members, and todos can be claimed,
released or taken over. On first launch it asks for the server address
(e.g. `http://navrobotec:27183`, over Tailscale or the LAN) and your name in
rooms; both can be changed later under ⋮ → Settings.

Build it with the Android SDK (`ANDROID_HOME` set, or `sdk.dir` in
`android/local.properties`) and a JDK 17+ that includes `javac`:

```sh
cd android
./gradlew assembleRelease     # app/build/outputs/apk/release/app-release.apk (~1.6 MB)
adb install -r app/build/outputs/apk/release/app-release.apk
```

The release APK is signed with the debug key, so it installs directly (allow
installing from unknown sources if you copy it to the phone instead).

## CLI (`todo`)

Categories are written as paths like `work/robotics` (case doesn't matter);
`/` means "no category" (Inbox) or "top level".

```sh
todo ls                         # grouped by category (also what `todo` does when piped)
todo ls pending -c work         # only Work and its subcategories
todo add Buy milk
todo add -c work/robotics Order servos
todo add -p high -d fri Send invoice   # priority high/medium/low, due next Friday
todo set 3 -p low -d 2026-10-31      # deadline: a date, today, tomorrow, +N, a weekday
todo set 3 -p none -d none           # clear both
todo add -m "Check the handoff first" Flash the board   # with a description
todo desc 3                          # show it; `todo desc 3 New text` sets it
todo desc 3 - < notes.md             # from stdin (keeps lines); `none` clears it
todo done 1 3                   # mark done (multiple ids ok)
todo undo 1
todo edit 2 New title
todo mv 4 5 home                # move todos; `todo mv 4 /` moves back to Inbox
todo rm 2
todo prune 4 7                  # move done todos to pruned; #4 and #7 are free again
todo clear                      # prune every done todo (same as `todo prune`)
todo ls pruned                  # they're p1, p2, ...
todo restore p2                 # back as a done todo, with a free id
todo rm p1                      # delete a pruned todo for good; `todo purge`: all

todo cat                        # category tree with open counts
todo cat add work/robotics/rover   # creates missing parents too
todo cat rename work/robotics Lab
todo cat mv work/lab /          # move to top level
todo cat rm work/lab            # its todos/subcategories move to its parent
todo cat archive work/lab       # hide it from lists; `todo cat ls -a` shows it
todo cat unarchive work/lab
todo cat style work fg=blue bold   # also bg=COLOR, italic, underline, no-bold, fg=none
todo cat style work none           # back to the plain look (or its parent's)
todo ls -c work/lab             # an archived category's todos
```

The server it talks to is, first match wins: `-s URL`, `$TODO_SERVER`, the first line
of `~/.config/todo/server`, else `http://127.0.0.1:27183`.

### Using it from another machine

The `todo` binary only needs the shared libraries it was built against, so on a
machine with the same distro base (e.g. Ubuntu 24.04 / Mint 22) you can copy it
and point it at this server, e.g. over Tailscale:

```sh
scp build/todo other-host:.local/bin/todo
ssh other-host 'mkdir -p ~/.config/todo && echo http://navrobotec:27183 > ~/.config/todo/server'
```

## API

| Method | Path                     | Body                                        | Notes                        |
|--------|--------------------------|---------------------------------------------|------------------------------|
| GET    | `/api/todos`             | –                                           | `?status=all\|pending\|done`, `&archived=1`, `&room=ID` |
| POST   | `/api/todos`             | `{"title", "categoryId"?, "roomId"?, "priority"?, "deadline"?, "description"?}` | 201 → created todo  |
| PATCH  | `/api/todos/{id}`        | `{"title"?, "done"?, "categoryId"?, "roomId"?, "priority"?, "deadline"?, "description"?}` | → updated todo |
| DELETE | `/api/todos/{id}`        | –                                           | 204                          |
| DELETE | `/api/todos/done`        | –                                           | deletes done todos → `{"removed": n}`; keeps archived |
| POST   | `/api/todos/{id}/prune`  | –                                           | → the pruned todo; 409 unless done |
| POST   | `/api/todos/prune`       | –                                           | prunes every done todo → `{"pruned": n}`; keeps archived |
| GET    | `/api/pruned`            | –                                           | pruned todos (own ids, `prunedAt`) |
| POST   | `/api/pruned/{id}/restore` | –                                         | 201 → the todo, done, with a free id |
| DELETE | `/api/pruned/{id}`       | –                                           | 204                          |
| DELETE | `/api/pruned`            | –                                           | → `{"removed": n}`           |
| GET    | `/api/categories`        | –                                           | flat list; `?archived=1`     |
| POST   | `/api/categories`        | `{"name", "parentId"?}`                     | 201; 409 if name taken       |
| PATCH  | `/api/categories/{id}`   | `{"name"?, "parentId"?, "archived"?, "style"?}` | 400 if it would nest in itself |
| DELETE | `/api/categories/{id}`   | –                                           | 204; contents move to parent |
| GET    | `/api/themes`            | –                                           | `[{"name", "colors": {role: "#rrggbb"}}]` |
| POST   | `/api/todos/{id}/claim`  | `{"agent"?, "force"?}`                      | → the todo; 409 if someone else has it |
| POST   | `/api/todos/{id}/release`| `{"agent"?, "force"?}`                      | → the todo; 409 if it's someone else's |
| GET    | `/api/rooms`             | –                                           | rooms with `members`, `cursor`, `prunedMessages`, `logVersion` |
| POST   | `/api/rooms`             | `{"name", "topic"?}`                        | 201; 409 if name taken       |
| GET    | `/api/rooms/{id}`        | –                                           | one room                     |
| PATCH  | `/api/rooms/{id}`        | `{"name"?, "topic"?}`                       | → the room                   |
| DELETE | `/api/rooms/{id}`        | –                                           | 204; deletes its messages, its todos stay |
| DELETE | `/api/rooms/{id}/members` | –                                          | `?name=NAME` → the room, without them |
| GET    | `/api/rooms/{id}/messages` | –                                         | `?since=SEQ&limit=N&wait=SECONDS` (long-poll, max 300); `&before=SEQ` pages back (no wait) |
| GET    | `/api/rooms/{id}/files`  | –                                           | the room's attachments, newest first, each with `messageId`, `seq`, `author`, `createdAt`; `?before=SEQ&limit=N` |
| POST   | `/api/rooms/{id}/messages` | `{"text", "author"?, "attachments"?}`     | 201 → the message; `text` may be `""` with attachments |
| POST   | `/api/rooms/{id}/messages/{mid}/prune` | –                             | → the pruned message (its pN id) |
| POST   | `/api/rooms/{id}/messages/prune` | `{"keep"?}`                         | prunes all but the newest `keep` (0) → `{"pruned": n}` |
| GET    | `/api/rooms/{id}/pruned` | –                                           | the room's pruned messages (own ids, `prunedAt`) |
| POST   | `/api/rooms/{id}/pruned/{pid}/restore` | –                             | 201 → the message, back in place, with a free id |
| DELETE | `/api/rooms/{id}/pruned/{pid}` | –                                     | 204                          |
| DELETE | `/api/rooms/{id}/pruned` | –                                           | → `{"removed": n}`           |
| POST   | `/api/files`             | the file                                    | multipart (field `file`), or the raw body with `?name=`; 201 → `{"id", "name", "type", "size", "width"?, "height"?, "duration"?}`; 400 for other types, 413 over 10 MB (50 MB for video) |
| GET    | `/api/files/{id}`        | –                                           | the file, with its type; `?name=` names it, `&download=1` saves it; honours `Range` (206) |
| POST   | `/mcp`                   | JSON-RPC                                    | MCP endpoint (see Rooms)     |

`categoryId` / `parentId` are an id, or `null` for none / top level.
`priority` is `"low"`, `"medium"`, `"high"` or `null`; `deadline` is `"YYYY-MM-DD"` or `null`;
`description` is text (up to 10000 characters, newlines kept) or `null`.
`attachments` (on todos and messages, left out when there are none) is a list
of uploaded files: send `[{"id", "name"?}]` (or bare ids; a todo's PATCH replaces
its list, `null` clears it) and the server fills in `type`, `size` and, for
images and videos, `width` / `height` (and a video's `duration` in seconds)
from the file itself, when its headers say.
A category's `style` is `null` or `{"fg"?, "bg"?: "#rrggbb", "bold"?, "italic"?,
"underline"?: bool}`; a PATCH replaces the whole style.
Without `archived=1`, the lists leave out archived categories, everything below
them, and their todos (so older clients simply don't see them). A category's
`archived` is only its own flag; one inside an archived category is archived too.
Todos also have `roomId` and `claimedBy` (an agent name or `null`). Requests that
change todos may send `X-Todo-Agent: NAME` so room events name who did it.
Message ids are per room and reused once pruned, so they don't say what's
newer: each message also has a `seq`, one counter for every room that only
grows. Logs are in `seq` order, and the last `seq` you've seen (or a room's
`cursor`) is your cursor: `GET .../messages?since=SEQ&wait=60` answers as soon
as there's a newer message, or with `[]` after 60 seconds. A room's
`logVersion` changes when messages are pruned or restored, so a client holding
the log knows to load it again. Messages have `kind` `"message"` or `"event"`
(written by the server, with the `todoId` it's about).

```json
{"id": 1, "title": "Buy milk", "done": false, "createdAt": 1790319063, "categoryId": 3,
 "priority": "high", "deadline": "2026-10-02", "description": "Semi-skimmed"}
{"id": 3, "name": "Robotics", "parentId": 1, "createdAt": 1790319063, "archived": false,
 "style": {"fg": "#4f7fe0", "bold": true}}
{"id": 1, "name": "rover", "topic": "Rover firmware", "createdAt": 1790319063,
 "members": [{"name": "claude-1", "lastSeen": 1790319100}], "cursor": 8,
 "prunedMessages": 0, "logVersion": 0}
{"id": 3, "seq": 8, "roomId": 1, "author": "claude-1", "text": "claimed #2 Write tests",
 "createdAt": 1790319100, "kind": "event", "todoId": 2}
```

Errors return `{"error": "..."}` with a 4xx status. The data file holds
`{"todos": [...], "categories": [...], "pruned": [...], "rooms": [...], "messages": [...], "prunedMessages": [...]}`; older files (a bare array of todos) load fine.
Attachments are stored as `files/ID` next to it.

## Claude usage

`todo usage` and the UI (status panel, and `U` for the full table) show the
Claude plan's 5-hour and 7-day usage plus a per-device, per-session token
index (input x1, output x5, cache-write x1.25, cache-read x0.1). Colours turn
yellow at 70% and red at 90%.

Nothing here calls a model. The status line script writes the plan limits to
`~/.claude/usage-status.json`; `tools/usage-collector.py` (one per machine,
every 60 s) adds a summary of the local session transcripts and POSTs it to
`/api/usage`. The server keeps only the latest report per device, in memory.
Install the collector with `tools/install-usage-collector.sh DEVICE`.
