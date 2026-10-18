#!/usr/bin/env python3
"""Reports this machine's Claude Code usage to the todo server (POST /api/usage).

Plain script, python3 stdlib only, no network calls except to the todo server.
Reads ~/.claude/usage-status.json (written by the status line) for the plan
percentages and incrementally scans ~/.claude/projects/*/*.jsonl for a
per-session token index (input*1 + output*5 + cache-write*1.25 + cache-read*0.1).
Only files touched in the last few hours are read, and each is read once from
where it stopped last time (state in ~/.cache/todo-usage/state.json).

    usage-collector.py [--device NAME] [--server URL] [--interval 60] [--once] [--dry-run]
"""
import argparse, collections, glob, json, os, re, socket, sys, time, urllib.request
from datetime import datetime

HOME = os.path.expanduser('~')
PROJECTS = HOME + '/.claude/projects'
STATUS = HOME + '/.claude/usage-status.json'
STATE = HOME + '/.cache/todo-usage/state.json'
WINDOW = 5 * 3600
KEEP = WINDOW + 600
AGENT_RE = re.compile(r'(?:TODO_AGENT=|--as )([A-Za-z][A-Za-z0-9_-]{1,38})')
W = (1.0, 5.0, 1.25, 0.1)


def server_url(arg):
    if arg:
        return arg
    if os.environ.get('TODO_SERVER'):
        return os.environ['TODO_SERVER']
    try:
        with open(HOME + '/.config/todo/server') as f:
            line = f.readline().strip()
            if line:
                return line
    except OSError:
        pass
    return 'http://127.0.0.1:27183'


def parse_ts(s):
    return datetime.fromisoformat(s.replace('Z', '+00:00')).timestamp()


def load_state():
    try:
        with open(STATE) as f:
            s = json.load(f)
        if s.get('v') == 1:
            return s
    except Exception:
        pass
    return {'v': 1, 'files': {}}


def save_state(st):
    try:
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        tmp = STATE + '.%d' % os.getpid()
        with open(tmp, 'w') as f:
            json.dump(st, f, separators=(',', ':'))
        os.replace(tmp, STATE)
    except OSError:
        pass


def scan(st, now):
    """Reads new lines of recently touched transcripts into st['files']."""
    files = st['files']
    live = set()
    for path in glob.glob(PROJECTS + '/*/*.jsonl'):
        try:
            sz = os.path.getsize(path)
            mt = os.path.getmtime(path)
        except OSError:
            continue
        if mt < now - KEEP:
            continue
        live.add(path)
        fs = files.get(path)
        if fs is None or sz < fs['off']:
            fs = files[path] = {'off': 0, 'cwd': '', 'agents': {}, 'msgs': {}}
        if sz == fs['off']:
            continue
        try:
            with open(path, 'rb') as f:
                f.seek(fs['off'])
                data = f.read()
        except OSError:
            continue
        end = data.rfind(b'\n') + 1  # only complete lines
        fs['off'] += end
        for raw in data[:end].splitlines():
            line = raw.decode('utf-8', 'ignore')
            if 'TODO_AGENT=' in line or '--as ' in line:
                for a in AGENT_RE.findall(line):
                    fs['agents'][a] = fs['agents'].get(a, 0) + 1
            if '"usage"' not in line:
                continue
            try:
                d = json.loads(line)
                m = d.get('message') or {}
                u = m['usage']
                t = parse_ts(d['timestamp'])
            except Exception:
                continue
            if t < now - KEEP or not isinstance(u, dict):
                continue
            if d.get('cwd'):
                fs['cwd'] = d['cwd']
            key = m.get('id') or d.get('uuid')
            if not key or key in fs['msgs']:
                continue
            idx = ((u.get('input_tokens') or 0) * W[0] + (u.get('output_tokens') or 0) * W[1]
                   + (u.get('cache_creation_input_tokens') or 0) * W[2]
                   + (u.get('cache_read_input_tokens') or 0) * W[3])
            mod = m.get('model') or ''
            fs['msgs'][key] = [round(t), d.get('sessionId') or os.path.basename(path)[:-6],
                               round(idx), '' if mod == '<synthetic>' else mod]
    for p in list(files):  # forget old files and old messages
        if p not in live:
            del files[p]
        else:
            ms = files[p]['msgs']
            for k in [k for k, v in ms.items() if v[0] < now - KEEP]:
                del ms[k]


def short_project(cwd):
    return '~' + cwd[len(HOME):] if cwd.startswith(HOME) else cwd


def summarize(st, now, window_start):
    sess = {}
    for path, fs in st['files'].items():
        agents = fs['agents']
        agent = max(agents, key=agents.get) if agents else ''
        cwd = fs['cwd']
        for t, sid, idx, mod in fs['msgs'].values():
            s = sess.get(sid)
            if s is None:
                s = sess[sid] = {'name': agent or os.path.basename(cwd.rstrip('/')) or sid[:8],
                                 'project': short_project(cwd), 'model': '', 'idx_30m': 0,
                                 'idx_window': 0, 'last': 0}
            if t >= now - 1800:
                s['idx_30m'] += idx
            if t >= window_start:
                s['idx_window'] += idx
            if t >= s['last'] and mod:
                s['last'], s['model'] = t, mod
    out = [s for s in sess.values() if s['idx_window'] > 0 or s['idx_30m'] > 0]
    out.sort(key=lambda s: -s['idx_window'])
    for s in out:
        s['model'] = s['model'].replace('claude-', '')
        s['idx_30m'], s['idx_window'] = round(s['idx_30m']), round(s['idx_window'])
        del s['last']
    return out[:30]


def build(st, device, now):
    rep = {'device': device, 'ts': now}
    try:
        with open(STATUS) as f:
            su = json.load(f)
        rep['status_ts'] = su.get('ts')
        rep['model'] = su.get('model') or ''
        for key, name in (('five_hour', 'five_hour'), ('seven_day', 'seven_day')):
            lim = su.get(key) or {}
            if 'used_percentage' in lim:
                rep[name + '_pct'] = lim['used_percentage']
            if 'resets_at' in lim:
                rep[name + '_resets_at'] = lim['resets_at']
    except Exception:
        pass
    ra = rep.get('five_hour_resets_at')
    start = ra - WINDOW if isinstance(ra, (int, float)) and ra > now else now - WINDOW
    rep['sessions'] = summarize(st, now, start)
    return rep


def post(url, rep):
    req = urllib.request.Request(url.rstrip('/') + '/api/usage', json.dumps(rep).encode(),
                                 {'Content-Type': 'application/json', 'X-Todo-Agent': 'usage-' + rep['device']})
    with urllib.request.urlopen(req, timeout=10) as r:
        r.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--device', default=os.environ.get('USAGE_DEVICE') or socket.gethostname().split('.')[0].lower())
    ap.add_argument('--server')
    ap.add_argument('--interval', type=int, default=60)
    ap.add_argument('--once', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    url = server_url(a.server)
    st = load_state()
    while True:
        now = int(time.time())
        try:
            scan(st, now)
            save_state(st)
            rep = build(st, a.device, now)
            if a.dry_run:
                print(json.dumps(rep, indent=1))
            else:
                post(url, rep)
        except Exception as e:  # server down etc: try again next round
            print('usage-collector:', e, file=sys.stderr)
            if a.once:
                return 1
        if a.once:
            return 0
        time.sleep(a.interval)


if __name__ == '__main__':
    sys.exit(main())
