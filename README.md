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

## Categories

Todos can be grouped into nested categories (e.g. `Robotics lab / Rover`). A todo
without a category lives in **Inbox**. Viewing a category shows its own todos plus
everything in its subcategories, grouped under small headings. Sibling categories
must have different names (case-insensitive), and names can't contain `/`.

Deleting a category never deletes todos: its todos and subcategories move up to
its parent.

In the web UI: pick a category in the sidebar (on phones, the ≡ button), and new
todos go into it. The `⋯` next to the open count has New subcategory / Rename /
Move / Delete. Move a todo with its folder icon, or drag it onto a sidebar
category; drag categories onto each other to nest them.

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
| `m` | move to category | move into another category |
| `d` | delete (asks) | delete (asks) |
| `space` | toggle done | fold / unfold |

Anywhere: `n` new todo, `c` jump to a category, `/` search (a number also matches
that todo id), `[` `]` all / pending / done, `H` hide / show completed, `C` clear
completed, `T` theme, `tab` `h`/`l` `1` `2` switch panel, `R` refresh, `q` quit.
Text fields support `ctrl-a`/`ctrl-e`/`ctrl-u`/`ctrl-k`/`ctrl-w`.

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

## CLI (`todo`)

Categories are written as paths like `work/robotics` (case doesn't matter);
`/` means "no category" (Inbox) or "top level".

```sh
todo ls                         # grouped by category (also what `todo` does when piped)
todo ls pending -c work         # only Work and its subcategories
todo add Buy milk
todo add -c work/robotics Order servos
todo done 1 3                   # mark done (multiple ids ok)
todo undo 1
todo edit 2 New title
todo mv 4 5 home                # move todos; `todo mv 4 /` moves back to Inbox
todo rm 2
todo clear                      # delete all completed

todo cat                        # category tree with open counts
todo cat add work/robotics/rover   # creates missing parents too
todo cat rename work/robotics Lab
todo cat mv work/lab /          # move to top level
todo cat rm work/lab            # its todos/subcategories move to its parent
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
| GET    | `/api/todos`             | –                                           | `?status=all\|pending\|done` |
| POST   | `/api/todos`             | `{"title", "categoryId"?}`                  | 201 → created todo           |
| PATCH  | `/api/todos/{id}`        | `{"title"?, "done"?, "categoryId"?}`        | → updated todo               |
| DELETE | `/api/todos/{id}`        | –                                           | 204                          |
| DELETE | `/api/todos/done`        | –                                           | → `{"removed": n}`           |
| GET    | `/api/categories`        | –                                           | flat list                    |
| POST   | `/api/categories`        | `{"name", "parentId"?}`                     | 201; 409 if name taken       |
| PATCH  | `/api/categories/{id}`   | `{"name"?, "parentId"?}`                    | 400 if it would nest in itself |
| DELETE | `/api/categories/{id}`   | –                                           | 204; contents move to parent |

`categoryId` / `parentId` are an id, or `null` for none / top level.

```json
{"id": 1, "title": "Buy milk", "done": false, "createdAt": 1790319063, "categoryId": 3}
{"id": 3, "name": "Robotics", "parentId": 1, "createdAt": 1790319063}
```

Errors return `{"error": "..."}` with a 4xx status. The data file holds
`{"todos": [...], "categories": [...]}`; older files (a bare array of todos) load fine.
