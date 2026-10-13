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

Data lives in `~/.local/share/todo_manager/todos.json`.
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

A todo can also have A todo can have a priority (high `!!!`, medium `!!`, low `!`)
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

## Interactive UI

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
`C` prune all done, `T` theme, `R` refresh, `q` quit.
Text fields support `ctrl-a`/`ctrl-e`/`ctrl-u`/`ctrl-k`/`ctrl-w`.

**Panels.** `tab` / `shift+tab` cycle through categories, todos and details; `1`
`2` `3` (or `h` / `l`) go straight to one. In details, `j`/`k` scroll a long
title; the other todo keys still act on the selected todo. `ctrl+←` / `ctrl+→`
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
| GET    | `/api/todos`             | –                                           | `?status=all\|pending\|done`, `&archived=1` |
| POST   | `/api/todos`             | `{"title", "categoryId"?, "priority"?, "deadline"?, "description"?}` | 201 → created todo  |
| PATCH  | `/api/todos/{id}`        | `{"title"?, "done"?, "categoryId"?, "priority"?, "deadline"?, "description"?}` | → updated todo |
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

`categoryId` / `parentId` are an id, or `null` for none / top level.
`priority` is `"low"`, `"medium"`, `"high"` or `null`; `deadline` is `"YYYY-MM-DD"` or `null`;
`description` is text (up to 10000 characters, newlines kept) or `null`.
A category's `style` is `null` or `{"fg"?, "bg"?: "#rrggbb", "bold"?, "italic"?,
"underline"?: bool}`; a PATCH replaces the whole style.
Without `archived=1`, the lists leave out archived categories, everything below
them, and their todos (so older clients simply don't see them). A category's
`archived` is only its own flag; one inside an archived category is archived too.

```json
{"id": 1, "title": "Buy milk", "done": false, "createdAt": 1790319063, "categoryId": 3,
 "priority": "high", "deadline": "2026-10-02", "description": "Semi-skimmed"}
{"id": 3, "name": "Robotics", "parentId": 1, "createdAt": 1790319063, "archived": false,
 "style": {"fg": "#4f7fe0", "bold": true}}
```

Errors return `{"error": "..."}` with a 4xx status. The data file holds
`{"todos": [...], "categories": [...], "pruned": [...]}`; older files (a bare array of todos) load fine.
