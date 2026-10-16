const $ = (sel, root = document) => root.querySelector(sel);
const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  node.append(...children);
  return node;
};
const tpl = $('#item-tpl');
const ALL = 'all';
const INBOX = 'inbox';
const ARCHIVE = 'archive';  // every archived category
const PRUNED = 'pruned';    // done todos moved aside to free their ids
const ROOM = 'room:';       // a room's view is ROOM + its id, e.g. 'room:3'

let todos = [];
let cats = [];
let pruned = [];  // their ids are separate from todos' (p1, p2...)
let rooms = [];
let roomsLoaded = false;
let view = ALL;           // ALL, INBOX, ARCHIVE, PRUNED, a category id or ROOM + id
let seen = null;          // ids already on screen, so only new rows animate in
let showDone = false;
let showArchive = false;  // the sidebar's Archived section is unfolded
let collapsed = new Set();
let me = 'human';         // your name in rooms
let lastRead = {};        // room id -> the newest message seq you've seen there

try {
  showDone = localStorage.getItem('showDone') === '1';
  showArchive = localStorage.getItem('showArchive') === '1';
  collapsed = new Set(JSON.parse(localStorage.getItem('collapsed') || '[]'));
  me = localStorage.getItem('agentName') || 'human';
  lastRead = JSON.parse(localStorage.getItem('roomRead') || '{}');
} catch {}

function remember() {
  try {
    localStorage.setItem('showDone', showDone ? '1' : '0');
    localStorage.setItem('showArchive', showArchive ? '1' : '0');
    localStorage.setItem('collapsed', JSON.stringify([...collapsed]));
    localStorage.setItem('agentName', me);
    localStorage.setItem('roomRead', JSON.stringify(lastRead));
  } catch {}
}

// ── API ──

async function api(method, path, body, { signal } = {}) {
  let res;
  // Says who you are, so changes to room todos are announced under your name.
  // Header values must be ASCII; other names go in ?agent= instead.
  const ascii = /^[\x20-\x7e]+$/.test(me);
  const headers = ascii ? { 'X-Todo-Agent': me } : {};
  if (!ascii && me) path += (path.includes('?') ? '&' : '?') + 'agent=' + encodeURIComponent(me);
  if (body) headers['Content-Type'] = 'application/json';
  try {
    res = await fetch('/api' + path, {
      method,
      headers,
      body: body ? JSON.stringify(body) : undefined,
      signal,
    });
  } catch (e) {
    if (e.name === 'AbortError') throw e;
    throw new Error("Can't reach the todo server. Check that it's running, then try again.");
  }
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    const msg = err.error || `The server returned an error (${res.status})`;
    throw new Error(msg.charAt(0).toUpperCase() + msg.slice(1) + '.');
  }
  return res.status === 204 ? null : res.json();
}

function showError(e) {
  const box = $('#error');
  box.textContent = e ? e.message : '';
  box.hidden = !e;
}

async function run(fn) {
  try {
    await fn();
    showError(null);
  } catch (e) {
    showError(e);
  }
  render();
}

// Everything, archived included; views filter it (see inView).
async function fetchAll() {
  const first = !cats.length;
  [todos, cats, pruned, rooms] = await Promise.all([
    api('GET', '/todos?archived=1'), api('GET', '/categories?archived=1'), api('GET', '/pruned'),
    api('GET', '/rooms')]);
  roomsLoaded = true;
  if (first) revealArchive();  // opened on an archived category's link
}

const load = () => run(fetchAll);

// ── Category tree ──

const byName = (a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' });
const findCat = (id) => cats.find((c) => c.id === id);
const children = (parentId) => cats.filter((c) => (c.parentId ?? null) === parentId).sort(byName);

function pathOf(id) {
  const parts = [];
  for (let c = findCat(id); c; c = findCat(c.parentId)) parts.unshift(c);
  return parts;
}
const pathName = (id, from = null) => {
  const start = from ? pathOf(from).length : 0;
  return pathOf(id).slice(start).map((c) => c.name).join(' / ');
};

function isWithin(id, ancestor) {
  for (let c = findCat(id); c; c = findCat(c.parentId)) if (c.id === ancestor) return true;
  return false;
}

// How category `id` looks: its own style, else the nearest ancestor's.
function styleOf(id) {
  for (let c = findCat(id); c; c = findCat(c.parentId)) if (c.style) return c.style;
  return null;
}

// Gives `node` category `id`'s colors and font style. As a `block` (a todo
// row, a sidebar item, a heading) a background fills the whole thing and its
// secondary colors are adjusted to suit; otherwise it's a small label.
const paintCat = (node, id, block = false) => paintStyle(node, styleOf(id), block);

function paintStyle(node, st, block = false) {
  if (!st) return node;
  // A background without a text color gets whichever reads better on it.
  const text = st.fg || (st.bg && (isDark(st.bg) ? '#f4f4f0' : '#1e2126'));
  if (text) {
    node.style.color = text;
    node.style.setProperty('--category', text);
  }
  if (st.bg) {
    node.style.background = st.bg;
    node.classList.add(block ? 'cat-block' : 'cat-chip');
    if (block) {
      node.style.setProperty('--text', text);
      node.style.setProperty('--muted', mix(text, st.bg, 72));
      node.style.setProperty('--faint', mix(text, st.bg, 45));
    }
  }
  if (st.bold) node.style.fontWeight = '600';
  if (st.italic) node.style.fontStyle = 'italic';
  if (st.underline) node.style.textDecoration = 'underline';
  return node;
}

// True if category `id` or one of its ancestors is archived.
function inArchive(id) {
  for (let c = findCat(id); c; c = findCat(c.parentId)) if (c.archived) return true;
  return false;
}
const viewArchived = (v = view) => v === ARCHIVE || (typeof v === 'number' && inArchive(v));

// The outermost archived categories, sorted by path.
const archiveRoots = () => cats.filter((c) => c.archived && !inArchive(c.parentId))
  .sort((a, b) => pathName(a.id).localeCompare(pathName(b.id), undefined, { sensitivity: 'base' }));

// Categories below `parentId` in display order, with depth. Archived ones are
// left out, unless `parentId` is itself in the archive.
function flatten(parentId = null, depth = 0, out = [], skipCollapsed = false) {
  const archived = inArchive(parentId);
  for (const c of children(parentId)) {
    if (c.archived && !archived) continue;
    out.push({ cat: c, depth });
    if (!(skipCollapsed && collapsed.has(c.id))) flatten(c.id, depth + 1, out, skipCollapsed);
  }
  return out;
}

// ── Rooms ──

const isRoom = (v = view) => typeof v === 'string' && v.startsWith(ROOM);
const roomIdOf = (v = view) => (isRoom(v) ? Number(v.slice(ROOM.length)) : null);
const findRoom = (id) => rooms.find((r) => r.id === id);
const unread = (r) => r.cursor > (lastRead[r.id] || 0);

// "just now", "5m ago", "3h ago", "2d ago".
function ago(seconds) {
  const s = Math.max(0, Date.now() / 1000 - seconds);
  if (s < 60) return 'just now';
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`;
  return `${Math.floor(s / 86400)}d ago`;
}

// Archived todos only show up inside archived categories.
function inView(todo, v = view) {
  if (isRoom(v)) return todo.roomId === roomIdOf(v);
  if (v === ALL) return !inArchive(todo.categoryId);
  if (v === INBOX) return todo.categoryId == null;
  if (v === ARCHIVE) return inArchive(todo.categoryId);
  return isWithin(todo.categoryId, v) && (inArchive(v) || !inArchive(todo.categoryId));
}

const openIn = (v) => todos.filter((t) => !t.done && inView(t, v)).length;

// ── Priority and deadline ──

const PRIORITY = { high: 3, medium: 2, low: 1 };
const MARKS = { high: '!!!', medium: '!!', low: '!' };

// Local dates as "YYYY-MM-DD", and day numbers for comparing them.
const isoDate = (d) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
const dayNum = (iso) => { const [y, m, d] = iso.split('-').map(Number); return Date.UTC(y, m - 1, d) / 864e5; };
const todayNum = () => dayNum(isoDate(new Date()));
const addDays = (n) => { const d = new Date(); d.setDate(d.getDate() + n); return isoDate(d); };

// Short and relative when near: today, tomorrow, Fri, 3d ago, Oct 5.
function dueLabel(iso) {
  const diff = dayNum(iso) - todayNum();
  if (diff === 0) return 'today';
  if (diff === 1) return 'tomorrow';
  if (diff === -1) return 'yesterday';
  if (diff < 0) return `${-diff}d ago`;
  const date = new Date(iso + 'T12:00');
  if (diff < 7) return date.toLocaleDateString(undefined, { weekday: 'short' });
  const sameYear = date.getFullYear() === new Date().getFullYear();
  return date.toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: sameYear ? undefined : 'numeric' });
}

// Higher priority first, then earlier deadline (none last), then oldest first.
function listOrder(a, b) {
  const p = (PRIORITY[b.priority] || 0) - (PRIORITY[a.priority] || 0);
  if (p) return p;
  if (a.deadline !== b.deadline) {
    if (!a.deadline || !b.deadline) return a.deadline ? -1 : 1;
    return a.deadline < b.deadline ? -1 : 1;
  }
  return a.id - b.id;
}
const viewTarget = () => (typeof view === 'number' ? view : null);

// ── Routing: #/inbox, #/archived, #/c/12 ──

function readHash() {
  const h = location.hash;
  if (h === '#/inbox') return INBOX;
  if (h === '#/archived') return ARCHIVE;
  if (h === '#/pruned') return PRUNED;
  const r = h.match(/^#\/r\/(\d+)$/);
  if (r) return ROOM + r[1];
  const m = h.match(/^#\/c\/(\d+)$/);
  return m ? Number(m[1]) : ALL;
}

function go(v) {
  const hash = v === ALL ? '#/' : v === INBOX ? '#/inbox' : v === ARCHIVE ? '#/archived' : v === PRUNED ? '#/pruned'
    : isRoom(v) ? `#/r/${roomIdOf(v)}` : `#/c/${v}`;
  if (location.hash !== hash) history.pushState(null, '', hash);
  view = v;
  revealArchive();
  $('#sidebar').classList.remove('open');
  $('#nav-toggle').setAttribute('aria-expanded', 'false');
  render();
}

// Unfolds the sidebar's Archived section when showing something archived.
function revealArchive() {
  if (viewArchived() && !showArchive) {
    showArchive = true;
    remember();
  }
}

window.addEventListener('popstate', () => { view = readHash(); revealArchive(); render(); });

// ── Rendering ──

function render() {
  // Don't rebuild the list under someone who is mid-edit.
  if (document.querySelector('.item .edit')) return;
  if (typeof view === 'number' && cats.length && !findCat(view)) view = ALL;
  if (isRoom() && roomsLoaded && !findRoom(roomIdOf())) view = ALL;

  syncPoll();
  renderSidebar();
  renderHeader();
  renderTodos();
  renderRoom();
  seen = new Set(todos.map((t) => t.id));
}

// `fold` is a category id, or ARCHIVE for the Archived section.
const isFolded = (fold) => (fold === ARCHIVE ? !showArchive : collapsed.has(fold));
function toggleFold(fold) {
  if (fold === ARCHIVE) showArchive = !showArchive;
  else collapsed.has(fold) ? collapsed.delete(fold) : collapsed.add(fold);
  remember();
  render();
}

function navRow(v, name, { depth = 0, fold = null } = {}) {
  const row = el('div', { className: 'nav-row' });
  row.style.paddingLeft = `${depth * 14}px`;
  if (typeof v === 'number' && inArchive(v)) row.classList.add('archived');

  if (fold) {
    const expanded = !isFolded(fold);
    const btn = el('button', { className: 'fold', ariaLabel: expanded ? 'Collapse' : 'Expand' });
    btn.setAttribute('aria-expanded', String(expanded));
    btn.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M6 4l4 4-4 4"/></svg>';
    btn.addEventListener('click', () => toggleFold(fold));
    row.append(btn);
  } else {
    row.append(el('span', { className: 'fold-spacer' }));
  }

  const n = v === PRUNED ? pruned.length : openIn(v);
  const item = el('button', { className: 'nav-item' },
    el('span', { className: 'nav-name', textContent: name }),
    el('span', { className: 'nav-count', textContent: n || '' }));
  const room = findRoom(roomIdOf(v));
  if (room && unread(room) && v !== view) {
    item.classList.add('unread');
    item.title = 'New messages';
  }
  if (v === view) item.setAttribute('aria-current', 'page');
  if (typeof v === 'number') paintCat(item, v, true);
  item.addEventListener('click', () => go(v));
  row.append(item);

  // Drop todos (and categories) onto a row to move them there.
  row.addEventListener('dragover', (e) => {
    if (!dragging || !canDrop(v)) return;
    e.preventDefault();
    row.classList.add('drop-target');
  });
  row.addEventListener('dragleave', () => row.classList.remove('drop-target'));
  row.addEventListener('drop', (e) => {
    e.preventDefault();
    row.classList.remove('drop-target');
    dropOn(v);
  });
  if (typeof v === 'number') {
    item.draggable = true;
    item.addEventListener('dragstart', (e) => {
      dragging = { kind: 'cat', id: v };
      e.dataTransfer.effectAllowed = 'move';
    });
    item.addEventListener('dragend', () => { dragging = null; });
  }
  return el('li', {}, row);
}

function renderSidebar() {
  $('#nav-fixed').replaceChildren(navRow(ALL, 'All'), navRow(INBOX, 'Inbox'));
  // Folds only if something shows below it (archived children don't, outside the archive).
  const catRow = ({ cat, depth }, name = cat.name) =>
    navRow(cat.id, name, { depth, fold: flatten(cat.id).length ? cat.id : null });
  $('#nav-tree').replaceChildren(...flatten(null, 0, [], true).map((r) => catRow(r)));

  // Archived: each outermost archived category by its full path, then its subtree.
  const roots = archiveRoots();
  const rows = [];
  if (roots.length) {
    rows.push(navRow(ARCHIVE, 'Archived', { fold: ARCHIVE }));
    if (showArchive) {
      for (const cat of roots) {
        rows.push(catRow({ cat, depth: 1 }, pathName(cat.id)));
        if (!collapsed.has(cat.id)) rows.push(...flatten(cat.id, 2, [], true).map((r) => catRow(r)));
      }
    }
  }
  if (pruned.length || view === PRUNED) rows.push(navRow(PRUNED, 'Pruned'));
  $('#nav-archive').replaceChildren(...rows);

  const roomRows = rooms.slice().sort(byName).map((r) => navRow(ROOM + r.id, r.name));
  if (roomRows.length) roomRows.unshift(el('li', { className: 'nav-heading', textContent: 'Rooms' }));
  $('#nav-rooms').replaceChildren(...roomRows);
}

function renderHeader() {
  const crumbs = $('#crumbs');
  if (view === ALL) {
    crumbs.replaceChildren(el('span', {
      textContent: new Date().toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long' }),
    }));
  } else if (isRoom()) {
    const room = findRoom(roomIdOf());
    crumbs.replaceChildren(el('span', { textContent: 'Rooms' }),
      el('span', { className: 'sep', textContent: '/', ariaHidden: 'true' }),
      el('span', { className: 'here', textContent: room ? room.name : '' }));
  } else if (view === INBOX || view === ARCHIVE || view === PRUNED) {
    const label = { [INBOX]: 'Inbox', [ARCHIVE]: 'Archived', [PRUNED]: 'Pruned' }[view];
    crumbs.replaceChildren(el('span', { className: 'here', textContent: label }));
  } else {
    const parts = pathOf(view);
    const nodes = [];
    parts.forEach((c, i) => {
      if (i) nodes.push(el('span', { className: 'sep', textContent: '/', ariaHidden: 'true' }));
      if (i === parts.length - 1) nodes.push(paintCat(el('span', { className: 'here', textContent: c.name }), c.id));
      else {
        const b = paintCat(el('button', { textContent: c.name }), c.id);
        b.addEventListener('click', () => go(c.id));
        nodes.push(b);
      }
    });
    if (inArchive(view)) nodes.push(el('span', { className: 'badge', textContent: 'Archived' }));
    crumbs.replaceChildren(...nodes);
  }

  const open = openIn(view);
  $('#open-count').textContent = open ? `${open} open` : '';
  $('#cat-menu-btn').hidden = typeof view !== 'number' && !isRoom();
  $('#cat-menu-btn .visually-hidden').textContent = isRoom() ? 'Room options' : 'Category options';
  for (const b of menu.querySelectorAll('[data-action]')) b.hidden = b.dataset.action.startsWith('room-') !== isRoom();
  const cat = findCat(view);
  const room = findRoom(roomIdOf());
  $('#new-title').placeholder = cat ? `Add to ${cat.name}` : room ? `Add to ${room.name}` : "What's next?";
  $('#add-form').hidden = view === ARCHIVE || view === PRUNED;
  $('#clear-done').hidden = viewArchived();  // the server keeps archived todos anyway

  // Only the archived category itself can be unarchived, not one inside it.
  const archiveItem = $('[data-action="archive"]', menu);
  archiveItem.textContent = cat?.archived ? 'Unarchive' : 'Archive';
  if (!isRoom()) archiveItem.hidden = !cat || (!cat.archived && inArchive(cat.id));
}

function renderTodos() {
  if (view === PRUNED) return renderPruned();
  const inThisView = todos.filter((t) => inView(t));
  const open = inThisView.filter((t) => !t.done).sort(listOrder);
  const done = inThisView.filter((t) => t.done).reverse();

  // Group open todos: the viewed category's own todos first, then one group
  // per subcategory that has any.
  const groups = [];
  const direct = (catId) => open.filter((t) => (t.categoryId ?? null) === catId);
  if (view === ALL) {
    groups.push({ id: null, todos: direct(null) });
    for (const { cat } of flatten()) groups.push({ id: cat.id, title: pathName(cat.id), todos: direct(cat.id) });
  } else if (view === INBOX || isRoom()) {
    groups.push({ id: null, todos: open });
  } else if (view === ARCHIVE) {
    for (const root of archiveRoots()) {
      groups.push({ id: root.id, title: pathName(root.id), todos: direct(root.id) });
      for (const { cat } of flatten(root.id)) groups.push({ id: cat.id, title: pathName(cat.id), todos: direct(cat.id) });
    }
  } else {
    groups.push({ id: view, todos: direct(view) });
    for (const { cat } of flatten(view)) groups.push({ id: cat.id, title: pathName(cat.id, view), todos: direct(cat.id) });
  }

  $('#open-groups').replaceChildren(...groups.filter((g) => g.todos.length).map((g) => {
    const section = el('section', { className: 'group' });
    if (g.title) {
      const link = el('button', { textContent: g.title });
      link.addEventListener('click', () => go(g.id));
      const heading = paintCat(el('h2', { className: 'group-title' }, link), g.id, true);
      if (styleOf(g.id)) link.style.color = 'inherit';
      section.append(heading);
    }
    section.append(el('ul', { className: 'list' }, ...g.todos.map((t) => renderItem(t, isRoom()))));
    return section;
  }));

  const empty = $('#empty');
  empty.hidden = open.length > 0;
  empty.textContent = isRoom()
    ? 'No open todos in this room. Add one above, or drop a todo on the room in the sidebar.'
    : view === ARCHIVE
    ? (archiveRoots().length ? 'Nothing open in archived categories.'
                             : 'Nothing archived. Archive a category from its ⋯ menu.')
    : view === ALL && !todos.length
      ? 'Nothing here yet. Type above and press Enter to add your first todo.'
      : 'Nothing open here. Type above and press Enter to add one.';

  $('#done-section').hidden = done.length === 0;
  $('#done-count').textContent = `${done.length} done`;
  $('#done-toggle').setAttribute('aria-expanded', String(showDone));
  $('#done-list').hidden = !showDone;
  $('#done-list').replaceChildren(...done.map((t) => renderItem(t, true)));
}

// Pruned todos, newest first: restore one (as done, with a free id) or delete
// it for good.
function renderPruned() {
  const list = pruned.slice().sort((a, b) => b.prunedAt - a.prunedAt || b.id - a.id);
  const rows = list.map((p) => {
    const li = tpl.content.firstElementChild.cloneNode(true);
    li.draggable = false;
    for (const sel of ['.plan', '.move', '.prune']) $(sel, li).remove();
    const check = $('.check', li);
    check.setAttribute('aria-checked', 'true');
    check.setAttribute('aria-label', `Restore "${p.title}"`);
    check.title = 'Restore (as done)';
    check.addEventListener('click', () => run(async () => {
      await api('POST', `/pruned/${p.id}/restore`);
      await fetchAll();
    }));
    $('.title', li).textContent = p.title;
    if (p.categoryId != null) $('.tag', li).textContent = pathName(p.categoryId);
    $('.due', li).textContent = 'pruned ' + new Date(p.prunedAt * 1000).toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
    const del = $('.delete', li);
    del.setAttribute('aria-label', 'Delete for good');
    del.title = 'Delete for good';
    del.addEventListener('click', () => run(async () => {
      await api('DELETE', `/pruned/${p.id}`);
      pruned = pruned.filter((x) => x.id !== p.id);
    }));
    paintCat(li, p.categoryId, true);
    return li;
  });

  const bar = [];
  if (list.length) {
    const purge = el('button', { className: 'quiet', textContent: 'Delete all for good' });
    purge.addEventListener('click', async () => {
      const ok = await confirmDialog('Delete all pruned todos?', `${list.length} todo(s) will be gone for good.`, 'Delete all');
      if (ok) run(async () => { await api('DELETE', '/pruned'); pruned = []; });
    });
    bar.push(el('div', { className: 'done-bar pruned-bar' },
      el('span', { textContent: 'Click a check to restore a todo (as done).' }), purge));
  }
  $('#open-groups').replaceChildren(...bar, el('ul', { className: 'list pruned-list' }, ...rows));
  const empty = $('#empty');
  empty.hidden = list.length > 0;
  empty.textContent = 'Nothing pruned. Pruning a done todo moves it here and gives its number to the next new todo.';
  $('#done-section').hidden = true;
}

function renderItem(todo, showTag) {
  const li = tpl.content.firstElementChild.cloneNode(true);
  if (seen && !seen.has(todo.id)) li.classList.add('new');

  const check = $('.check', li);
  check.setAttribute('aria-checked', String(todo.done));
  check.setAttribute('aria-label', todo.done ? `Mark "${todo.title}" as open` : `Mark "${todo.title}" as done`);
  check.addEventListener('click', () => run(async () => {
    Object.assign(todo, await api('PATCH', `/todos/${todo.id}`, { done: !todo.done }));
  }));

  const title = $('.title', li);
  title.textContent = todo.title;
  if (todo.description) {
    const desc = $('.desc', li);
    desc.textContent = todo.description;
    desc.hidden = false;
    desc.title = 'Click to show all / less';
    desc.addEventListener('click', () => desc.classList.toggle('open'));
  }
  title.title = 'Added ' + new Date(todo.createdAt * 1000).toLocaleString();
  title.addEventListener('dblclick', () => startEdit(title, todo));
  title.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') startEdit(title, todo);
  });

  if (todo.priority) {
    const prio = $('.prio', li);
    prio.textContent = MARKS[todo.priority];
    prio.dataset.p = todo.priority;
    prio.title = `${todo.priority[0].toUpperCase()}${todo.priority.slice(1)} priority`;
  }
  if (todo.deadline) {
    const due = $('.due', li);
    const diff = dayNum(todo.deadline) - todayNum();
    due.textContent = dueLabel(todo.deadline);
    due.title = 'Due ' + new Date(todo.deadline + 'T12:00').toLocaleDateString(undefined, { dateStyle: 'full' });
    if (!todo.done && diff < 0) due.classList.add('overdue');
    if (!todo.done && diff === 0) due.classList.add('today');
  }
  $('.plan', li).addEventListener('click', () => planTodo(todo));
  const prune = $('.prune', li);
  prune.hidden = !todo.done;
  prune.addEventListener('click', () => run(async () => {
    await api('POST', `/todos/${todo.id}/prune`);
    await fetchAll();
  }));
  paintCat(li, todo.categoryId, true);  // todos look like their category

  // Done todos are listed flat, so say where each one lives.
  if (showTag && todo.categoryId != null && todo.categoryId !== view) {
    $('.tag', li).textContent = pathName(todo.categoryId, typeof view === 'number' ? view : null);
  }

  // Rooms: which room it's shared in, who has claimed it, and claim / release.
  const room = findRoom(todo.roomId);
  if (room && !isRoom()) {
    const tag = $('.room-tag', li);
    tag.textContent = '# ' + room.name;
    tag.title = `In room ${room.name}`;
  }
  if (todo.claimedBy) {
    const badge = $('.claimed', li);
    badge.textContent = todo.claimedBy === me ? 'you' : todo.claimedBy;
    badge.title = `Claimed by ${todo.claimedBy}`;
    badge.classList.toggle('mine', todo.claimedBy === me);
  }
  const claim = $('.claim', li);
  claim.hidden = todo.done || (!room && !todo.claimedBy);
  const label = !todo.claimedBy ? 'Claim' : todo.claimedBy === me ? 'Release' : `Release ${todo.claimedBy}'s claim`;
  claim.setAttribute('aria-label', label);
  claim.title = label;
  claim.classList.toggle('held', !!todo.claimedBy);
  claim.addEventListener('click', () => claimTodo(todo));

  $('.move', li).addEventListener('click', () => moveTodo(todo));
  $('.delete', li).addEventListener('click', () => run(async () => {
    await api('DELETE', `/todos/${todo.id}`);
    todos = todos.filter((t) => t.id !== todo.id);
  }));

  li.addEventListener('dragstart', (e) => {
    dragging = { kind: 'todo', id: todo.id };
    e.dataTransfer.effectAllowed = 'move';
    li.classList.add('dragging');
  });
  li.addEventListener('dragend', () => { dragging = null; li.classList.remove('dragging'); });
  return li;
}

// Claims an unclaimed todo, or releases a claim (someone else's after asking).
async function claimTodo(todo) {
  let action = 'claim', force = false;
  if (todo.claimedBy) {
    action = 'release';
    if (todo.claimedBy !== me) {
      force = await confirmDialog(`Release ${todo.claimedBy}'s claim?`,
        `${todo.claimedBy} may still be working on “${todo.title}”. Releasing lets anyone claim it.`, 'Release');
      if (!force) return;
    }
  }
  run(async () => {
    Object.assign(todo, await api('POST', `/todos/${todo.id}/${action}`, { agent: me, force }));
  });
}

function startEdit(title, todo) {
  const input = el('input', { className: 'edit', value: todo.title, maxLength: 500 });
  input.setAttribute('aria-label', 'Edit todo');
  title.replaceWith(input);
  input.focus();

  let finished = false;
  const finish = (save) => {
    if (finished) return;
    finished = true;
    input.remove();
    const value = input.value.trim();
    if (!save || !value || value === todo.title) return render();
    run(async () => {
      Object.assign(todo, await api('PATCH', `/todos/${todo.id}`, { title: value }));
    });
  };
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') finish(true);
    if (e.key === 'Escape') finish(false);
  });
  input.addEventListener('blur', () => finish(true));
}

// ── Drag and drop ──

let dragging = null;  // { kind: 'todo' | 'cat', id }

function canDrop(v) {
  if (v === PRUNED) return false;
  if (isRoom(v)) return dragging.kind === 'todo';  // shares it in the room
  // Dropping a category on Archived archives it.
  if (v === ARCHIVE) return dragging.kind === 'cat' && !inArchive(dragging.id);
  if (dragging.kind === 'todo') return v !== ALL;
  // Categories go to the top level (All) or into a category outside themselves.
  if (v === INBOX) return false;
  return v === ALL || !isWithin(v, dragging.id);
}

function dropOn(v) {
  const d = dragging;
  dragging = null;
  if (!d) return;
  if (d.kind === 'todo' && isRoom(v)) {
    run(() => api('PATCH', `/todos/${d.id}`, { roomId: roomIdOf(v) }).then(fetchAll));
  } else if (d.kind === 'todo') {
    run(() => api('PATCH', `/todos/${d.id}`, { categoryId: v === INBOX ? null : v }).then(fetchAll));
  } else if (v === ARCHIVE) {
    run(() => api('PATCH', `/categories/${d.id}`, { archived: true }).then(fetchAll));
  } else {
    run(() => api('PATCH', `/categories/${d.id}`, { parentId: v === ALL ? null : v }).then(fetchAll));
  }
}

// ── Dialogs ──

const dialog = $('#dialog');

// Shows the dialog built by `build(close)`; resolves with what close() gets.
function openDialog(build) {
  return new Promise((resolve) => {
    let result;
    const close = (value) => { result = value; dialog.close(); };
    dialog.replaceChildren(...build(close));
    dialog.addEventListener('close', () => resolve(result), { once: true });
    dialog.showModal();
  });
}

function actions(close, confirmLabel, { danger = false } = {}) {
  const cancel = el('button', { type: 'button', className: 'quiet', textContent: 'Cancel' });
  cancel.addEventListener('click', () => close(undefined));
  const ok = el('button', { type: 'submit', className: 'primary' + (danger ? ' danger' : ''), textContent: confirmLabel });
  return el('div', { className: 'dialog-actions' }, cancel, ok);
}

// Asks for a name; `submit(name)` may throw to show an error and keep the dialog open.
function promptName(title, value, confirmLabel, submit) {
  return openDialog((close) => {
    const input = el('input', { value, maxLength: 60, required: true });
    input.setAttribute('aria-label', 'Name');
    const error = el('p', { className: 'dialog-error', role: 'alert' });
    const form = el('form', {}, input, error, actions(close, confirmLabel));
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const name = input.value.trim();
      if (!name) return;
      try {
        await submit(name);
        close(true);
      } catch (err) {
        error.textContent = err.message;
      }
    });
    setTimeout(() => input.select());
    return [el('h2', { textContent: title }), form];
  });
}

function confirmDialog(title, text, confirmLabel) {
  return openDialog((close) => {
    const form = el('form', {}, el('p', { textContent: text }), actions(close, confirmLabel, { danger: true }));
    form.addEventListener('submit', (e) => { e.preventDefault(); close(true); });
    return [el('h2', { textContent: title }), form];
  });
}

// Lets the user choose a destination; resolves with a category id, null for
// the top level / Inbox, or undefined if cancelled.
function pickCategory(title, rootLabel, current, exclude = null) {
  return openDialog((close) => {
    const options = [{ id: null, label: rootLabel, depth: 0 }]
      .concat(flatten().map(({ cat, depth }) => ({ id: cat.id, label: cat.name, depth })));
    const list = el('ul', { className: 'pick-list' }, ...options.map((o) => {
      const b = el('button', { type: 'button', textContent: o.label });
      b.style.paddingLeft = `${8 + o.depth * 16}px`;
      if (o.id === current) b.setAttribute('aria-current', 'true');
      if (exclude != null && o.id != null && isWithin(o.id, exclude)) b.disabled = true;
      b.addEventListener('click', () => close(o.id));
      return el('li', {}, b);
    }));
    const cancel = el('button', { type: 'button', className: 'quiet', textContent: 'Cancel' });
    cancel.addEventListener('click', () => close(undefined));
    setTimeout(() => ($('[aria-current="true"]', list) || $('button', list))?.focus());
    return [el('h2', { textContent: title }), list, el('div', { className: 'dialog-actions' }, cancel)];
  });
}

async function moveTodo(todo) {
  const dest = await pickCategory(`Move “${todo.title}” to`, 'Inbox (no category)', todo.categoryId ?? null);
  if (dest === undefined || dest === (todo.categoryId ?? null)) return;
  run(async () => {
    Object.assign(todo, await api('PATCH', `/todos/${todo.id}`, { categoryId: dest }));
  });
}

// Description, priority buttons and a date field; saves only what changed.
async function planTodo(todo) {
  let priority = todo.priority ?? null;
  const result = await openDialog((close) => {
    const seg = el('div', { className: 'segmented', role: 'group' });
    seg.setAttribute('aria-label', 'Priority');
    const choices = [[null, 'None'], ['low', 'Low'], ['medium', 'Medium'], ['high', 'High']];
    const buttons = choices.map(([value, label]) => {
      const b = el('button', { type: 'button', textContent: label });
      b.addEventListener('click', () => {
        priority = value;
        buttons.forEach((x, i) => x.setAttribute('aria-pressed', String(choices[i][0] === priority)));
      });
      b.setAttribute('aria-pressed', String(value === priority));
      return b;
    });
    seg.append(...buttons);

    const date = el('input', { type: 'date', value: todo.deadline ?? '' });
    date.setAttribute('aria-label', 'Deadline');
    const quick = (label, days) => {
      const b = el('button', { type: 'button', className: 'quiet', textContent: label });
      b.addEventListener('click', () => { date.value = days == null ? '' : addDays(days); });
      return b;
    };
    const desc = el('textarea', { value: todo.description ?? '', maxLength: 10000, placeholder: 'Notes, links, steps… (optional)' });
    desc.setAttribute('aria-label', 'Description');
    // Ctrl/Cmd+Enter saves from the text box (Enter alone is a new line).
    desc.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); form.requestSubmit(); }
    });
    const form = el('form', {},
      el('p', { className: 'field-label', textContent: 'Description' }), desc,
      el('p', { className: 'field-label', textContent: 'Priority' }), seg,
      el('p', { className: 'field-label', textContent: 'Deadline' }),
      el('div', { className: 'date-row' }, date, quick('Today', 0), quick('Tomorrow', 1), quick('Clear', null)),
      actions(close, 'Save'));
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      close({ priority, deadline: date.value || null, description: desc.value.trimEnd() || null });
    });
    setTimeout(() => ($('[aria-pressed="true"]', seg) || buttons[0]).focus());
    return [el('h2', { textContent: todo.title }), form];
  });
  if (!result) return;
  const patch = {};
  if (result.priority !== (todo.priority ?? null)) patch.priority = result.priority;
  if (result.deadline !== (todo.deadline ?? null)) patch.deadline = result.deadline;
  if (result.description !== (todo.description ?? null)) patch.description = result.description;
  if (!Object.keys(patch).length) return;
  run(async () => {
    Object.assign(todo, await api('PATCH', `/todos/${todo.id}`, patch));
  });
}

// Named colors, the same list the CLI and terminal UI accept.
const PALETTE = {
  red: '#e5534b', orange: '#e08a3c', yellow: '#c9a227', green: '#57a65a', teal: '#2aa198',
  blue: '#4f7fe0', purple: '#9a6ce0', pink: '#d9579b', brown: '#a0724a', gray: '#8a8f98',
};

// Colors and font style for a category's name and tags, with a live preview.
async function styleCategory(cat) {
  const st = { fg: null, bg: null, bold: false, italic: false, underline: false, ...(cat.style || {}) };
  const inherited = !cat.style && styleOf(cat.parentId);
  const result = await openDialog((close) => {
    const preview = el('span', { className: 'style-preview', textContent: cat.name });
    const update = () => {
      preview.removeAttribute('style');
      preview.classList.remove('cat-chip');
      // With nothing set, it looks like its parent.
      paintStyle(preview, Object.values(st).some(Boolean) ? st : styleOf(cat.parentId));
    };

    const colorRow = (key) => {
      const row = el('div', { className: 'swatch-row', role: 'group' });
      const custom = el('input', { type: 'color', value: st[key] || '#888888', title: 'Custom color' });
      const buttons = [];
      const mark = () => {
        for (const b of buttons) b.setAttribute('aria-pressed', String((b.dataset.color || null) === st[key]));
        custom.classList.toggle('picked', !!st[key] && !Object.values(PALETTE).includes(st[key]));
      };
      const pick = (color) => { st[key] = color; mark(); update(); };
      const none = el('button', { type: 'button', className: 'swatch none', title: 'None' });
      none.addEventListener('click', () => pick(null));
      buttons.push(none);
      for (const [name, hex] of Object.entries(PALETTE)) {
        const b = el('button', { type: 'button', className: 'swatch', title: name });
        b.dataset.color = hex;
        b.style.background = hex;
        b.addEventListener('click', () => pick(hex));
        buttons.push(b);
      }
      custom.addEventListener('input', () => pick(custom.value));
      row.append(...buttons, custom);
      mark();
      return row;
    };

    const toggles = el('div', { className: 'segmented' });
    for (const [key, label, css] of [['bold', 'Bold', 'font-weight:600'], ['italic', 'Italic', 'font-style:italic'], ['underline', 'Underline', 'text-decoration:underline']]) {
      const b = el('button', { type: 'button', textContent: label, style: css });
      b.setAttribute('aria-pressed', String(!!st[key]));
      b.addEventListener('click', () => {
        st[key] = !st[key];
        b.setAttribute('aria-pressed', String(st[key]));
        update();
      });
      toggles.append(b);
    }

    const clear = el('button', { type: 'button', className: 'quiet', textContent: 'Reset' });
    clear.addEventListener('click', () => close({ reset: true }));
    const acts = actions(close, 'Save');
    acts.prepend(clear);
    acts.style.justifyContent = 'flex-end';
    const form = el('form', {},
      el('p', { className: 'field-label', textContent: 'Text color' }), colorRow('fg'),
      el('p', { className: 'field-label', textContent: 'Background' }), colorRow('bg'),
      el('p', { className: 'field-label', textContent: 'Font' }), toggles,
      acts);
    form.addEventListener('submit', (e) => { e.preventDefault(); close({ style: st }); });
    update();
    const hint = inherited ? el('p', { className: 'small', textContent: 'Now uses its parent\'s style. Setting one here overrides it.' }) : '';
    return [el('h2', {}, 'Style for ', preview), hint, form];
  });
  if (!result) return;
  const style = result.reset ? null : Object.fromEntries(Object.entries(result.style).filter(([, v]) => v));
  run(async () => {
    await api('PATCH', `/categories/${cat.id}`, { style: style && Object.keys(style).length ? style : null });
    await fetchAll();
  });
}

async function newCategory(parentId) {
  const parent = findCat(parentId);
  await promptName(parent ? `New category in ${parent.name}` : 'New category', '', 'Create', async (name) => {
    const cat = await api('POST', '/categories', { name, parentId });
    await fetchAll();
    if (parentId != null) collapsed.delete(parentId);
    remember();
    go(cat.id);
  });
}

// ── Category menu ──

// ── Themes ──
// The server lists its themes (the same .theme files the TUI uses). Each one
// only sets some roles; the rest keep this page's own colors.

const THEME_VARS = {
  bg: '--bg',
  fg: '--text',
  muted: '--muted',
  border: '--faint',
  border_active: '--accent',
  selection_bg_inactive: '--hover',
  error: '--danger',
  category: '--category',
  done: '--done',
  pending: '--warn',
};
let themeName = 'default';
try { themeName = localStorage.getItem('theme') || 'default'; } catch {}

const isDark = (hex) => {
  const n = parseInt(hex.slice(1), 16);
  return 0.299 * (n >> 16) + 0.587 * ((n >> 8) & 255) + 0.114 * (n & 255) < 128;
};
const mix = (a, b, pct) => `color-mix(in srgb, ${a} ${pct}%, ${b})`;

// CSS properties for a theme's colors. A theme with its own background also
// gets text colors that suit it, even if it doesn't set them.
function themeVars(colors) {
  const vars = {};
  for (const [role, name] of Object.entries(THEME_VARS)) if (colors[role]) vars[name] = colors[role];
  const bg = colors.bg;
  if (bg) {
    const dark = isDark(bg);
    const text = vars['--text'] ??= dark ? '#e7e5e0' : '#1e2126';
    vars['--muted'] ??= mix(text, bg, 55);
    vars['--faint'] ??= mix(text, bg, 22);
    vars['--hover'] ??= mix(text, 'transparent', 6);
    vars['--raised'] = mix(bg, text, 94);
    vars['color-scheme'] = dark ? 'dark' : 'light';
  }
  return vars;
}

function applyTheme(vars) {
  const root = document.documentElement.style;
  for (const name of [...Object.values(THEME_VARS), '--raised', 'color-scheme']) root.removeProperty(name);
  for (const [name, value] of Object.entries(vars)) root.setProperty(name, value);
}

function saveTheme(name, vars) {
  themeName = name;
  try {
    localStorage.setItem('theme', name);
    localStorage.setItem('themeVars', JSON.stringify(vars));  // applied early by index.html
  } catch {}
}

// Re-applies the saved theme from the server's copy, so edits to a theme
// file show up on the next load.
async function refreshTheme() {
  const themes = await api('GET', '/themes').catch(() => null);
  const theme = themes?.find((t) => t.name === themeName);
  if (!theme) return;
  const vars = themeVars(theme.colors);
  applyTheme(vars);
  saveTheme(theme.name, vars);
}

// Previews themes as you move through the list; Esc puts the old one back.
async function pickTheme() {
  let themes;
  try {
    themes = await api('GET', '/themes');
  } catch (e) {
    return showError(e);
  }
  const before = document.documentElement.getAttribute('style') || '';
  const chosen = await openDialog((close) => {
    const list = el('ul', { className: 'pick-list' }, ...themes.map((t) => {
      const b = el('button', { type: 'button', textContent: t.name });
      const swatch = el('span', { className: 'swatches' }, ...['bg', 'fg', 'border_active', 'done', 'error']
        .filter((r) => t.colors[r]).map((r) => el('i', { style: `background:${t.colors[r]}` })));
      b.append(swatch);
      if (t.name === themeName) b.setAttribute('aria-current', 'true');
      if (t.warnings) b.title = t.warnings.join('\n');
      b.addEventListener('focus', () => applyTheme(themeVars(t.colors)));
      b.addEventListener('mouseenter', () => b.focus());
      b.addEventListener('click', () => close(t));
      return el('li', {}, b);
    }));
    list.addEventListener('keydown', (e) => {
      const step = { ArrowDown: 1, j: 1, ArrowUp: -1, k: -1 }[e.key];
      if (!step) return;
      e.preventDefault();
      const buttons = [...list.querySelectorAll('button')];
      const i = buttons.indexOf(document.activeElement);
      buttons[Math.max(0, Math.min(buttons.length - 1, i + step))].focus();
    });
    const cancel = el('button', { type: 'button', className: 'quiet', textContent: 'Cancel' });
    cancel.addEventListener('click', () => close(undefined));
    const hint = el('p', { className: 'small', textContent: 'Add your own in ~/.config/todo/themes on the server.' });
    setTimeout(() => ($('[aria-current="true"]', list) || $('button', list))?.focus());
    return [el('h2', { textContent: 'Theme' }), list, hint, el('div', { className: 'dialog-actions' }, cancel)];
  });
  if (chosen) {
    const vars = themeVars(chosen.colors);
    applyTheme(vars);
    saveTheme(chosen.name, vars);
  } else {
    document.documentElement.setAttribute('style', before);
  }
}

const menuBtn = $('#cat-menu-btn');
const menu = $('#cat-menu');

function toggleMenu(open) {
  menu.hidden = !open;
  menuBtn.setAttribute('aria-expanded', String(open));
  if (open) $('button', menu).focus();
}

menuBtn.addEventListener('click', (e) => {
  e.stopPropagation();
  toggleMenu(menu.hidden);
});
document.addEventListener('click', (e) => {
  if (!menu.hidden && !menu.contains(e.target)) toggleMenu(false);
});
menu.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') { toggleMenu(false); menuBtn.focus(); }
});

menu.addEventListener('click', async (e) => {
  const action = e.target.closest('button')?.dataset.action;
  const cat = findCat(view);
  toggleMenu(false);
  if (action?.startsWith('room-')) return roomAction(action);
  if (!action || !cat) return;

  if (action === 'add-sub') return newCategory(cat.id);
  if (action === 'style') return styleCategory(cat);

  if (action === 'rename') {
    await promptName(`Rename ${cat.name}`, cat.name, 'Rename', async (name) => {
      await api('PATCH', `/categories/${cat.id}`, { name });
      await fetchAll();
    });
    return render();
  }

  if (action === 'move') {
    const dest = await pickCategory(`Move ${cat.name} into`, 'Top level', cat.parentId ?? null, cat.id);
    if (dest === undefined || dest === (cat.parentId ?? null)) return;
    return run(async () => {
      await api('PATCH', `/categories/${cat.id}`, { parentId: dest });
      await fetchAll();
    });
  }

  if (action === 'archive') {
    const archived = !cat.archived;
    return run(async () => {
      await api('PATCH', `/categories/${cat.id}`, { archived });
      await fetchAll();
      // An archived category moves to the Archived section; stay on it there.
      revealArchive();
    });
  }

  if (action === 'delete') {
    const parent = findCat(cat.parentId);
    const where = parent ? parent.name : 'the top level';
    const hasStuff = todos.some((t) => t.categoryId === cat.id) || children(cat.id).length;
    const ok = await confirmDialog(`Delete ${cat.name}?`,
      hasStuff ? `Its todos and subcategories move to ${where}. Nothing else is deleted.`
               : 'It has no todos or subcategories.',
      'Delete category');
    if (!ok) return;
    run(async () => {
      await api('DELETE', `/categories/${cat.id}`);
      await fetchAll();
      go(parent ? parent.id : ALL);
    });
  }
});

// ── Room view: topic, members, message log ──

// Messages are ordered by `seq`, which only grows; their `id`s are reused
// once pruned, so they say nothing about order.
let log = { roomId: null, messages: [] };  // the open room's messages, oldest first
let poll = null;                           // { roomId, version, ctl: AbortController }
// The open room's pruned messages, when showing them instead of the log.
// `key` notes what they were loaded for, to load them again when it changes.
let prunedLog = { on: false, roomId: null, key: null, messages: [] };

// Asks for multi-line text; resolves with it (maybe empty), or undefined if cancelled.
function promptText(title, value, confirmLabel, placeholder) {
  return openDialog((close) => {
    const area = el('textarea', { value, maxLength: 500, placeholder });
    area.setAttribute('aria-label', title);
    const form = el('form', {}, area, actions(close, confirmLabel));
    form.addEventListener('submit', (e) => { e.preventDefault(); close(area.value.trim()); });
    area.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); form.requestSubmit(); }
    });
    setTimeout(() => area.select());
    return [el('h2', { textContent: title }), form];
  });
}

async function newRoom() {
  await promptName('New room', '', 'Create', async (name) => {
    const room = await api('POST', '/rooms', { name });
    await fetchAll();
    go(ROOM + room.id);
  });
}

async function roomAction(action) {
  const room = findRoom(roomIdOf());
  if (!room) return;
  if (action === 'room-rename') {
    await promptName(`Rename ${room.name}`, room.name, 'Rename', async (name) => {
      await api('PATCH', `/rooms/${room.id}`, { name });
      await fetchAll();
    });
    return render();
  }
  if (action === 'room-topic') {
    const topic = await promptText(`Topic for ${room.name}`, room.topic ?? '', 'Save', 'What this room is for');
    if (topic === undefined) return;
    return run(async () => {
      await api('PATCH', `/rooms/${room.id}`, { topic: topic || null });
      await fetchAll();
    });
  }
  if (action === 'room-prune') {
    const keep = await promptPrune(room);
    if (keep === undefined) return;
    return run(async () => {
      await api('POST', `/rooms/${room.id}/messages/prune`, { keep });
      await fetchAll();
    });
  }
  if (action === 'room-delete') {
    const ok = await confirmDialog(`Delete ${room.name}?`,
      'Its messages are deleted. Its todos stay, just not in a room.', 'Delete room');
    if (!ok) return;
    run(async () => {
      await api('DELETE', `/rooms/${room.id}`);
      await fetchAll();
      go(ALL);
    });
  }
}

// Asks how many of the newest messages to keep; resolves with that, or
// undefined if cancelled.
function promptPrune(room) {
  return openDialog((close) => {
    const keep = el('input', { type: 'number', min: 0, value: 0, required: true, className: 'keep' });
    keep.setAttribute('aria-label', 'Messages to keep');
    const form = el('form', {},
      el('p', { textContent: 'They move to Pruned, where you can restore them, and their numbers go to new messages.' }),
      el('label', { className: 'keep-row' }, 'Keep the newest ', keep, ' messages'),
      actions(close, 'Prune', { danger: true }));
    form.addEventListener('submit', (e) => {
      e.preventDefault();
      close(Math.max(0, parseInt(keep.value, 10) || 0));
    });
    setTimeout(() => keep.select());
    return [el('h2', { textContent: `Prune messages in ${room.name}?` }), form];
  });
}

async function changeName() {
  await promptName('Your name in rooms', me, 'Save', async (name) => {
    me = name.slice(0, 40);
    remember();
  });
  render();
}

function renderRoom() {
  const room = findRoom(roomIdOf());
  $('#room-info').hidden = !room;
  $('#room-log-section').hidden = !room;
  if (!room) return;

  const topic = $('#room-topic');
  topic.textContent = room.topic || 'No topic yet.';
  topic.classList.toggle('none', !room.topic);

  // Most recently seen first; you're always listed.
  const members = room.members.slice().sort((a, b) => b.lastSeen - a.lastSeen);
  const you = el('button', { className: 'member you', textContent: `${me} (you)`, title: 'Change your name' });
  you.addEventListener('click', changeName);
  $('#room-members').replaceChildren(you, ...members.filter((m) => m.name !== me).map((m) => {
    // Forgetting a member stops @suggestions of them; they rejoin if they come back.
    const forget = el('button', { className: 'forget', ariaLabel: `Remove ${m.name} from the room`,
      title: `Remove ${m.name} (they rejoin if they post or check the room again)` });
    forget.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M5 5l6 6M11 5l-6 6"/></svg>';
    forget.addEventListener('click', () => run(async () => {
      await api('DELETE', `/rooms/${room.id}/members?name=${encodeURIComponent(m.name)}`);
      await fetchAll();
    }));
    return el('span', { className: 'member', title: `Last seen ${new Date(m.lastSeen * 1000).toLocaleString()}` },
      m.name, el('span', { className: 'seen', textContent: ' · ' + ago(m.lastSeen) }), forget);
  }));

  $('#room-text').placeholder = `Message as ${me}. Enter sends, Shift+Enter for a new line`;
  renderLog();
}

// A stable color per name, so each author and their @mentions read alike.
function nameHue(name) {
  let h = 2166136261;  // FNV-1a, then spread so similar names differ
  for (const c of name.toLowerCase()) h = Math.imul(h ^ c.codePointAt(0), 16777619);
  h ^= h >>> 15;
  return (Math.imul(h, 2246822507) >>> 0) % 360;
}

// Message text with @mentions and #refs picked out in color.
function richText(text) {
  const out = [];
  const re = /(^|[^\w@#])(@[\w-]+(?:\.[\w-]+)*|#\d+)/g;
  let last = 0;
  for (let m; (m = re.exec(text));) {
    const start = m.index + m[1].length;
    out.push(text.slice(last, start));
    const tok = m[2];
    if (tok[0] === '@') {
      const span = el('span', { className: 'mention', textContent: tok });
      span.style.setProperty('--hue', nameHue(tok.slice(1)));
      if (tok.slice(1) === me) span.classList.add('me');
      out.push(span);
    } else {
      out.push(el('span', { className: 'ref', textContent: tok }));
    }
    last = start + tok.length;
  }
  out.push(text.slice(last));
  return out;
}

const ICONS = {
  prune: '<circle cx="4.5" cy="11.5" r="2"/><circle cx="11.5" cy="11.5" r="2"/><path d="M5.8 10L12 2.5M10.2 10L4 2.5"/>',
  restore: '<path d="M3.5 8a4.5 4.5 0 1 0 1.3-3.2M4.5 2.5v2.5H7"/>',
  delete: '<path d="M4.5 4.5l7 7M11.5 4.5l-7 7"/>',
};

function iconButton(icon, label, onClick) {
  const b = el('button', { className: 'quiet icon ' + icon, title: label, ariaLabel: label });
  b.innerHTML = `<svg viewBox="0 0 16 16" aria-hidden="true">${ICONS[icon]}</svg>`;
  b.addEventListener('click', onClick);
  return b;
}

// One message; `tools` are its hover buttons. Its number shows on hover of the time.
function messageRow(m, tools) {
  const when = new Date(m.createdAt * 1000);
  const time = el('time', {
    className: 'msg-time',
    textContent: when.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' }),
    title: `${m.prunedAt ? 'p' : '#'}${m.id} · ${when.toLocaleString()}`,
  });
  const author = el('span', { className: 'msg-author', textContent: m.author ?? 'server' });
  if (m.author === me) author.classList.add('mine');
  else if (m.author) author.style.setProperty('--hue', nameHue(m.author));
  const bar = el('span', { className: 'msg-tools' }, ...tools);
  if (m.kind === 'event') return el('li', { className: 'msg event' }, author, ' ', el('span', {}, ...richText(m.text)), time, bar);
  return el('li', { className: 'msg' }, el('div', { className: 'msg-head' }, author, time, bar),
    el('p', { className: 'msg-text' }, ...richText(m.text)));
}

function renderLog() {
  const box = $('#room-log');
  const room = findRoom(roomIdOf());
  if (prunedLog.roomId !== room?.id) prunedLog = { on: false, roomId: room?.id, key: null, messages: [] };
  const showPruned = prunedLog.on && room;
  if (showPruned) loadPruned(room);

  const toggle = $('#log-pruned');
  const n = room?.prunedMessages || 0;
  toggle.hidden = !showPruned && !n;
  toggle.textContent = showPruned ? 'Back to messages' : `${n} pruned`;
  $('.log-title').textContent = showPruned ? 'Pruned messages' : 'Messages';
  $('#room-post').hidden = showPruned;
  $('#log-pruned-bar').hidden = !showPruned || !prunedLog.messages.length;
  box.classList.toggle('pruned', !!showPruned);

  const atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 24;
  const msgs = showPruned ? prunedLog.messages : log.roomId === roomIdOf() ? log.messages : [];
  box.replaceChildren(...msgs.map((m) => showPruned
    ? messageRow(m, [
      iconButton('restore', 'Restore', () => run(async () => {
        await api('POST', `/rooms/${room.id}/pruned/${m.id}/restore`);
        await fetchAll();
      })),
      iconButton('delete', 'Delete for good', () => run(async () => {
        await api('DELETE', `/rooms/${room.id}/pruned/${m.id}`);
        prunedLog.messages = prunedLog.messages.filter((x) => x !== m);
        await fetchAll();
      })),
    ])
    : messageRow(m, [
      iconButton('prune', 'Prune (frees its number)', () => run(async () => {
        await api('POST', `/rooms/${room.id}/messages/${m.id}/prune`);
        await fetchAll();
      })),
    ])));

  const empty = $('#room-log-empty');
  empty.hidden = msgs.length > 0;
  empty.textContent = showPruned
    ? 'Nothing pruned here.'
    : "No messages yet. Agents post here, and changes to this room's todos show up too.";
  const shown = `${log.roomId}:${showPruned ? 'pruned' : 'log'}`;
  if (atBottom || box.dataset.room !== shown) box.scrollTop = box.scrollHeight;
  box.dataset.room = shown;
  markRead();
}

// Loads the room's pruned messages when they've changed since last time.
async function loadPruned(room) {
  const key = `${room.logVersion}:${room.prunedMessages}`;
  if (prunedLog.key === key) return;
  prunedLog.key = key;
  try {
    const msgs = await api('GET', `/rooms/${room.id}/pruned`);
    if (prunedLog.roomId !== room.id || prunedLog.key !== key) return;
    prunedLog.messages = msgs;
    renderLog();
  } catch (e) {
    prunedLog.key = null;
    showError(e);
  }
}

$('#log-pruned').addEventListener('click', () => {
  prunedLog.on = !prunedLog.on;
  renderLog();
});

$('#log-purge').addEventListener('click', async () => {
  const room = findRoom(roomIdOf());
  if (!room) return;
  const ok = await confirmDialog('Delete all pruned messages?',
    `${prunedLog.messages.length} message(s) will be gone for good.`, 'Delete all');
  if (!ok) return;
  run(async () => {
    await api('DELETE', `/rooms/${room.id}/pruned`);
    prunedLog = { ...prunedLog, on: false, messages: [] };
    await fetchAll();
  });
});

// Messages on screen count as read while the page is visible.
function markRead() {
  const id = roomIdOf();
  if (id == null || log.roomId !== id || document.hidden || !log.messages.length) return;
  const last = log.messages[log.messages.length - 1].seq;
  if ((lastRead[id] || 0) >= last) return;
  lastRead[id] = last;
  remember();
}

// Adds messages (skipping ones already there); true if any were events.
function addMessages(msgs) {
  const have = new Set(log.messages.map((m) => m.seq));
  const fresh = msgs.filter((m) => !have.has(m.seq));
  log.messages.push(...fresh);
  log.messages.sort((a, b) => a.seq - b.seq);
  const room = findRoom(log.roomId);
  if (room && fresh.length) room.cursor = Math.max(room.cursor, ...fresh.map((m) => m.seq));
  return fresh.some((m) => m.kind === 'event');
}

// Keeps one long poll running for the open room: it loads the recent
// messages, then waits for new ones until you leave the room. Starts over
// when the room's logVersion changes (messages were pruned or restored).
function syncPoll() {
  const id = roomIdOf();
  const version = findRoom(id)?.logVersion;
  if (poll?.roomId === id) {
    poll.version ??= version;
    if (version == null || poll.version === version) return;
  }
  poll?.ctl.abort();
  poll = null;
  if (id == null) return;
  poll = { roomId: id, version, ctl: new AbortController() };
  if (log.roomId !== id) log = { roomId: id, messages: [] };
  pollLoop(poll);
}

async function pollLoop(p) {
  const { signal } = p.ctl;
  const agent = encodeURIComponent(me);
  let delay = 1000;
  try {
    const msgs = await api('GET', `/rooms/${p.roomId}/messages?limit=200&agent=${agent}`, null, { signal });
    log = { roomId: p.roomId, messages: [] };  // replaces what was there, pruned ones and all
    addMessages(msgs);
    renderLog();
  } catch (e) {
    if (signal.aborted) return;
  }
  while (!signal.aborted) {
    const since = log.messages.length ? log.messages[log.messages.length - 1].seq : 0;
    try {
      const msgs = await api('GET', `/rooms/${p.roomId}/messages?since=${since}&wait=25&agent=${agent}`, null, { signal });
      delay = 1000;
      if (signal.aborted || log.roomId !== p.roomId) return;
      if (addMessages(msgs)) await fetchAll();  // a room todo changed
      render();
    } catch (e) {
      if (signal.aborted) return;
      // Server down or restarting: wait a bit longer each time.
      await new Promise((r) => setTimeout(r, delay));
      delay = Math.min(delay * 2, 30000);
    }
  }
}

$('#room-post').addEventListener('submit', (e) => {
  e.preventDefault();
  const box = $('#room-text');
  const text = box.value.trim();
  const id = roomIdOf();
  if (!text || id == null) return;
  run(async () => {
    const msg = await api('POST', `/rooms/${id}/messages`, { text, author: me });
    box.value = '';
    updateMentions();
    if (log.roomId === id) addMessages([msg]);
  });
});
// ── @mentions: suggest the room's members while typing "@name" ──

const mention = { matches: [], sel: 0 };

// The "@..." word the caret is at the end of, or null.
function mentionWord(box) {
  const before = box.value.slice(0, box.selectionStart);
  const m = /(^|\s)@([^\s@]*)$/.exec(before);
  return box.selectionStart === box.selectionEnd && m ? { start: before.length - m[2].length - 1, prefix: m[2] } : null;
}

function updateMentions() {
  const box = $('#room-text');
  const word = mentionWord(box);
  const room = findRoom(roomIdOf());
  let matches = [];
  if (word && room) {
    const prefix = word.prefix.toLowerCase();
    matches = room.members
      .filter((m) => m.name !== me && m.name.toLowerCase().startsWith(prefix))
      .sort((a, b) => b.lastSeen - a.lastSeen);
    // Nothing left to complete once it's typed out in full.
    if (matches.length === 1 && matches[0].name.toLowerCase() === prefix) matches = [];
  }
  mention.matches = matches;
  mention.sel = Math.min(mention.sel, Math.max(matches.length - 1, 0));
  const list = $('#mentions');
  list.hidden = !matches.length;
  list.replaceChildren(...matches.map((m, i) => {
    const li = el('li', { role: 'option' }, '@' + m.name, el('span', { className: 'seen', textContent: ' · ' + ago(m.lastSeen) }));
    li.setAttribute('aria-selected', String(i === mention.sel));
    // mousedown, not click, so the box keeps focus.
    li.addEventListener('mousedown', (e) => { e.preventDefault(); insertMention(i); });
    return li;
  }));
  list.children[mention.sel]?.scrollIntoView({ block: 'nearest' });
}

function insertMention(i) {
  const box = $('#room-text');
  const word = mentionWord(box);
  const m = mention.matches[i];
  if (!word || !m) return;
  const text = '@' + m.name + ' ';
  box.setRangeText(text, word.start, box.selectionStart, 'end');
  mention.sel = 0;
  updateMentions();
}

$('#room-text').addEventListener('input', () => { mention.sel = 0; updateMentions(); });
$('#room-text').addEventListener('click', updateMentions);
$('#room-text').addEventListener('keyup', (e) => {
  if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) updateMentions();
});
$('#room-text').addEventListener('blur', () => { $('#mentions').hidden = true; });
$('#room-text').addEventListener('keydown', (e) => {
  // While suggestions show: ↑/↓ pick one, Tab or Enter puts it in, Esc closes.
  if (!$('#mentions').hidden && !e.isComposing) {
    const n = mention.matches.length;
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      mention.sel = (mention.sel + (e.key === 'ArrowDown' ? 1 : -1) + n) % n;
      updateMentions();
      return;
    }
    if (e.key === 'Tab' || (e.key === 'Enter' && !e.shiftKey)) {
      e.preventDefault();
      insertMention(mention.sel);
      return;
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      $('#mentions').hidden = true;
      return;
    }
  }
  if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
    e.preventDefault();
    $('#room-post').requestSubmit();
  }
  if (e.key === 'Escape') e.target.blur();
});
document.addEventListener('visibilitychange', () => { if (!document.hidden) { markRead(); render(); } });

// ── Other events ──

$('#add-form').addEventListener('submit', (e) => {
  e.preventDefault();
  const input = $('#new-title');
  const title = input.value.trim();
  if (!title) return;
  run(async () => {
    const body = isRoom() ? { title, roomId: roomIdOf() } : { title, categoryId: viewTarget() };
    todos.push(await api('POST', '/todos', body));
    input.value = '';
  });
});

$('#new-category').addEventListener('click', () => newCategory(null));
$('#new-room').addEventListener('click', newRoom);
$('#theme-btn').addEventListener('click', pickTheme);

$('#nav-toggle').addEventListener('click', () => {
  const open = $('#sidebar').classList.toggle('open');
  $('#nav-toggle').setAttribute('aria-expanded', String(open));
});

$('#done-toggle').addEventListener('click', () => {
  showDone = !showDone;
  remember();
  render();
});

// Prunes every done todo (the server keeps those in archived categories).
$('#clear-done').addEventListener('click', () => run(async () => {
  await api('POST', '/todos/prune');
  await fetchAll();
}));

// "/" or "n" jumps to the add field from anywhere; "t" picks a theme.
document.addEventListener('keydown', (e) => {
  if (dialog.open) return;
  const typing = e.target.matches('input, textarea');
  if (!typing && (e.key === '/' || e.key === 'n') && !e.metaKey && !e.ctrlKey && !e.altKey) {
    e.preventDefault();
    $('#new-title').focus();
  }
  if (!typing && e.key === 't' && !e.metaKey && !e.ctrlKey && !e.altKey) {
    e.preventDefault();
    pickTheme();
  }
  if (typing && e.key === 'Escape' && e.target.id === 'new-title') e.target.blur();
});

// Start in the add field on desktop; on phones that would pop the keyboard.
if (matchMedia('(hover: hover)').matches) $('#new-title').focus();

// Stay in sync with the CLI / TUI: refresh on focus and every few seconds,
// but not while a dialog or menu is open.
const refresh = () => { if (!dialog.open && menu.hidden && !dragging) load(); };
window.addEventListener('focus', refresh);
setInterval(() => { if (!document.hidden) refresh(); }, 5000);

view = readHash();
load();
refreshTheme();
