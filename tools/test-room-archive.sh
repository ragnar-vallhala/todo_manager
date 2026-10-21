#!/bin/sh
# End-to-end check of room archiving against a throwaway server instance.
# Usage: tools/test-room-archive.sh [PORT]   (run from the repo, after building)
set -u
cd "$(dirname "$0")/.."
PORT=${1:-27997}
T=$(mktemp -d)
mkdir -p "$T/data"
build/todo_server -H 127.0.0.1 -p "$PORT" -d "$T/data/todos.json" -w public >"$T/log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; rm -rf "$T"' EXIT
sleep 1.5
U=http://127.0.0.1:$PORT
export TODO_AGENT=tester
todo() { build/todo -s "$U" "$@"; }
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
has() { echo "$1" | grep -q -- "$2"; }
noids() { sed 's/^\[#[0-9]*\]/[#]/'; }
counts() { curl -s "$U/api/rooms?archived=1" | python3 -c "import sys,json; [print(r['name'],r['cursor'],r['prunedMessages']) for r in json.load(sys.stdin) if r['name']=='alpha']"; }

echo hi > "$T/f.txt"
todo room add alpha First room >/dev/null
todo room add beta Second >/dev/null
todo room say alpha one >/dev/null
todo room say alpha --file "$T/f.txt" two >/dev/null
todo room say beta other >/dev/null
todo add -r alpha "a todo" >/dev/null
todo room say alpha three >/dev/null
todo room prune alpha 1 >/dev/null          # a message pruned by hand before archiving
before=$(todo room read alpha)
files_before=$(todo room files alpha)
counts > "$T/rooms.before"

todo room archive alpha | grep -q Archived && ok "archive" || bad "archive"
has "$(todo room ls)" alpha && bad "ls hides archived" || ok "ls hides archived"
has "$(todo room ls)" beta && ok "ls shows others" || bad "ls shows others"
has "$(todo room ls -a)" '\[archived\]' && ok "ls -a marks [archived]" || bad "ls -a marks"
out=$(todo room say alpha nope 2>&1); rc=$?
[ $rc -ne 0 ] && has "$out" "room is archived, unarchive first" && ok "post rejected: $out" || bad "post rejected ($rc) $out"
todo room show alpha >/dev/null 2>&1 && ok "show works" || bad "show works"
todo room read alpha >/dev/null 2>&1 && ok "read works" || bad "read works"
[ -z "$(todo room read alpha)" ] && ok "log empty (moved to pruned)" || bad "log not empty"
pr=$(todo room pruned alpha)
has "$pr" one && has "$pr" two && has "$pr" three && ok "pruned lists all 3" || bad "pruned lists: $pr"
has "$pr" f.txt && ok "attachment kept on its pruned message" || bad "attachment kept"
has "$(todo ls -r alpha)" "a todo" && ok "todos kept" || bad "todos kept"
has "$(todo room show alpha)" tester && ok "members kept" || bad "members kept"
todo room say beta another >/dev/null; has "$(todo room read beta)" '\[#2' && ok "beta ids unaffected" || bad "beta ids"
todo room say alpha x >/dev/null 2>&1 && bad "still rejected" || ok "still rejected"

todo room unarchive alpha | grep -q Unarchived && ok "unarchive" || bad "unarchive"
has "$(todo room ls)" alpha && ok "ls shows alpha again" || bad "ls shows alpha again"
after=$(todo room read alpha)
has "$after" two && has "$after" three && ok "archived messages back" || bad "messages back: $after"
has "$after" one && bad "hand-pruned message stays pruned" || ok "hand-pruned message stays pruned"
has "$(todo room pruned alpha)" one && ok "hand-pruned still in pruned" || bad "hand-pruned still pruned"
[ "$(todo room files alpha | noids)" = "$(echo "$files_before" | noids)" ] && ok "files identical" || bad "files differ"
counts > "$T/rooms.after"
cmp -s "$T/rooms.before" "$T/rooms.after" && ok "cursor and pruned counts identical" || { bad "counts differ"; diff "$T/rooms.before" "$T/rooms.after"; }
[ "$(echo "$after" | noids)" = "$(echo "$before" | noids)" ] && ok "read output identical (ids renumbered: #1 was free)" || { bad "read differs"; echo "$before"; echo "$after"; }
todo room say alpha again >/dev/null && ok "post works after unarchive" || bad "post after unarchive"

# persistence: restart the server on the same data and check the flag
todo room archive alpha >/dev/null
kill $PID; wait $PID 2>/dev/null
build/todo_server -H 127.0.0.1 -p "$PORT" -d "$T/data/todos.json" -w public >>"$T/log" 2>&1 &
PID=$!; sleep 1.5
has "$(todo room ls -a)" '\[archived\]' && ok "archived flag persists" || bad "persist"
todo room unarchive alpha >/dev/null
has "$(todo room read alpha)" again && ok "restore after restart" || bad "restore after restart"

# old data files (no archived field) load as not archived
kill $PID; wait $PID 2>/dev/null
python3 - "$T/data/todos.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p))
for r in d['rooms']: r.pop('archived',None); r.pop('archivedSeqs',None)
json.dump(d,open(p,'w'))
PY
build/todo_server -H 127.0.0.1 -p "$PORT" -d "$T/data/todos.json" -w public >>"$T/log" 2>&1 &
PID=$!; sleep 1.5
has "$(todo room ls)" alpha && ! has "$(todo room ls -a)" '\[archived\]' && ok "old files load, not archived" || bad "old files"
[ $fail = 0 ] && echo "ALL PASSED" || echo "FAILURES"
exit $fail
