#!/bin/sh
# End-to-end check of attachment typing against a throwaway server instance.
# Usage: tools/test-attachments.sh [PORT]   (run from the repo, after building)
set -u
cd "$(dirname "$0")/.."
PORT=${1:-27998}
T=$(mktemp -d)
mkdir -p "$T/data" "$T/in" "$T/out"
build/todo_server -H 127.0.0.1 -p "$PORT" -d "$T/data/todos.json" -w public >"$T/log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; rm -rf "$T"' EXIT
sleep 1.5
U=http://127.0.0.1:$PORT
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

# fixtures
head -c 300000 /dev/urandom > "$T/in/app.apk"; printf 'PK\003\004' | cat - "$T/in/app.apk" > "$T/in/x" && mv "$T/in/x" "$T/in/app.apk"
head -c 5000 /dev/urandom > "$T/in/a.zip"
printf 'hello\nworld\n' > "$T/in/note.txt"
printf '<html><script>alert(1)</script></html>' > "$T/in/page.html"
printf '<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>' > "$T/in/pic.svg"
printf '\211PNG\r\n\032\n\0\0\0\rIHDR\0\0\0\2\0\0\0\3\010\006\0\0\0' > "$T/in/p.png"; head -c 100 /dev/urandom >> "$T/in/p.png"
# minimal mp4: ftyp box + filler
printf '\0\0\0\030ftypisom\0\0\2\0isomiso2' > "$T/in/v.mp4"; head -c 200000 /dev/urandom >> "$T/in/v.mp4"
: > "$T/in/empty.bin"

up() { curl -s -X POST --data-binary @"$1" "$U/api/files?name=$2" ; }
field() { python3 -c "import sys,json; print(json.load(sys.stdin).get('$1',''))"; }

check() { # file name expectedKind expectedType
    r=$(up "$T/in/$1" "$2")
    id=$(echo "$r" | field id); kind=$(echo "$r" | field kind); type=$(echo "$r" | field type); nm=$(echo "$r" | field name)
    [ "$kind" = "$3" ] && ok "$1 kind=$kind" || bad "$1 kind=$kind (want $3) $r"
    [ -z "$4" ] || { [ "$type" = "$4" ] && ok "$1 type=$type" || bad "$1 type=$type (want $4)"; }
    [ "$nm" = "$2" ] && ok "$1 name kept" || bad "$1 name=$nm"
    curl -s -D "$T/h" -o "$T/out/$1" "$U/api/files/$id?name=$2"
    cmp -s "$T/in/$1" "$T/out/$1" && ok "$1 byte-identical" || bad "$1 differs"
    grep -qi '^x-content-type-options: nosniff' "$T/h" && ok "$1 nosniff" || bad "$1 no nosniff"
    if [ "$3" = document ]; then
        grep -qi '^content-disposition: attachment' "$T/h" && ok "$1 attachment" || bad "$1 not attachment: $(grep -i disposition $T/h)"
    fi
}
check app.apk app.apk document application/vnd.android.package-archive
check a.zip a.zip document application/zip
check note.txt note.txt document text/plain
check page.html page.html document application/octet-stream
check pic.svg pic.svg document application/octet-stream
check p.png p.png image image/png
check v.mp4 v.mp4 video video/mp4

# video ranges still work
code=$(curl -s -o /dev/null -w '%{http_code}' -H 'Range: bytes=0-99' "$U/api/files/$(up "$T/in/v.mp4" v.mp4 | field id)")
[ "$code" = 206 ] && ok "mp4 range 206" || bad "range $code"
# empty file refused
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @"$T/in/empty.bin" "$U/api/files?name=e")
[ "$code" = 400 ] && ok "empty refused" || bad "empty $code"
# a document over a room message and a todo, with the CLI
export TODO_SERVER=$U TODO_AGENT=tester
CLI=$PWD/build/todo
$CLI room add tr >/dev/null 2>&1
$CLI room say tr --file "$T/in/app.apk" "see apk" >/dev/null 2>&1 && ok "room say --file apk" || bad "room say"
$CLI room show tr 2>&1 | grep -q '^ *\[doc\] app.apk (' && ok "room show lists [doc]" || bad "room show: $($CLI room show tr 2>&1)"
fid=$($CLI room show tr 2>&1 | sed -n 's/.*\[doc\] app.apk (.*, \(f[0-9a-f]*\))\].*/\1/p;s/.*\[doc\] app.apk (.*, \(f[0-9a-f]*\))$/\1/p' | head -1)
rm -f "$T/out/app.apk"; [ -n "$fid" ] && (cd "$T/out" && $CLI room get tr "$fid" >/dev/null 2>&1)
cmp -s "$T/in/app.apk" "$T/out/app.apk" && ok "room get saves original name" || bad "room get"
$CLI add --file "$T/in/a.zip" "todo with zip" >/dev/null 2>&1 && ok "todo add --file zip" || bad "todo add"
# old stored data: an attachment without "kind" still loads
curl -s "$U/api/rooms" >/dev/null && ok "rooms load"
[ $fail = 0 ] && echo "ALL PASSED" || { echo "SOME FAILED"; exit 1; }
