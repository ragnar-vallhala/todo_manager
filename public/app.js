const $ = (sel, root = document) => root.querySelector(sel);
const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  node.append(...children);
  return node;
};
const tpl = $('#item-tpl');
const ALL = 'all';
const INBOX = 'inbox';

let todos = [];
let cats = [];
let view = ALL;           // ALL, INBOX, or a category id
let seen = null;          // ids already on screen, so only new rows animate in
let showDone = false;
let collapsed = new Set();

try {
  showDone = localStorage.getItem('showDone') === '1';
  collapsed = new Set(JSON.parse(localStorage.getItem('collapsed') || '[]'));
} catch {}

function remember() {
  try {
    localStorage.setItem('showDone', showDone ? '1' : '0');
    localStorage.setItem('collapsed', JSON.stringify([...collapsed]));
  } catch {}
}

// ── API ──

async function api(method, path, body) {
  let res;
  try {
    res = await fetch('/api' + path, {
      method,
      headers: body ? { 'Content-Type': 'application/json' } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch {
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

async function fetchAll() {
  [todos, cats] = await Promise.all([api('GET', '/todos'), api('GET', '/categories')]);
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

// Categories below `parentId` in display order, with depth.
function flatten(parentId = null, depth = 0, out = [], skipCollapsed = false) {
  for (const c of children(parentId)) {
    out.push({ cat: c, depth });
    if (!(skipCollapsed && collapsed.has(c.id))) flatten(c.id, depth + 1, out, skipCollapsed);
  }
  return out;
}

function inView(todo, v = view) {
  if (v === ALL) return true;
  if (v === INBOX) return todo.categoryId == null;
  return isWithin(todo.categoryId, v);
}

const openIn = (v) => todos.filter((t) => !t.done && inView(t, v)).length;
const viewTarget = () => (typeof view === 'number' ? view : null);

// ── Routing: #/inbox, #/c/12 ──

function readHash() {
  const h = location.hash;
  if (h === '#/inbox') return INBOX;
  const m = h.match(/^#\/c\/(\d+)$/);
  return m ? Number(m[1]) : ALL;
}

function go(v) {
  const hash = v === ALL ? '#/' : v === INBOX ? '#/inbox' : `#/c/${v}`;
  if (location.hash !== hash) history.pushState(null, '', hash);
  view = v;
  $('#sidebar').classList.remove('open');
  $('#nav-toggle').setAttribute('aria-expanded', 'false');
  render();
}

window.addEventListener('popstate', () => { view = readHash(); render(); });

// ── Rendering ──

function render() {
  // Don't rebuild the list under someone who is mid-edit.
  if (document.querySelector('.item .edit')) return;
  if (typeof view === 'number' && cats.length && !findCat(view)) view = ALL;

  renderSidebar();
  renderHeader();
  renderTodos();
  seen = new Set(todos.map((t) => t.id));
}

function navRow(v, name, { depth = 0, fold = null } = {}) {
  const row = el('div', { className: 'nav-row' });
  row.style.paddingLeft = `${depth * 14}px`;

  if (fold) {
    const expanded = !collapsed.has(fold);
    const btn = el('button', { className: 'fold', ariaLabel: expanded ? 'Collapse' : 'Expand' });
    btn.setAttribute('aria-expanded', String(expanded));
    btn.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M6 4l4 4-4 4"/></svg>';
    btn.addEventListener('click', () => {
      collapsed.has(fold) ? collapsed.delete(fold) : collapsed.add(fold);
      remember();
      render();
    });
    row.append(btn);
  } else {
    row.append(el('span', { className: 'fold-spacer' }));
  }

  const n = openIn(v);
  const item = el('button', { className: 'nav-item' },
    el('span', { className: 'nav-name', textContent: name }),
    el('span', { className: 'nav-count', textContent: n || '' }));
  if (v === view) item.setAttribute('aria-current', 'page');
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
  $('#nav-tree').replaceChildren(...flatten(null, 0, [], true).map(({ cat, depth }) =>
    navRow(cat.id, cat.name, { depth, fold: children(cat.id).length ? cat.id : null })));
}

function renderHeader() {
  const crumbs = $('#crumbs');
  if (view === ALL) {
    crumbs.replaceChildren(el('span', {
      textContent: new Date().toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long' }),
    }));
  } else if (view === INBOX) {
    crumbs.replaceChildren(el('span', { className: 'here', textContent: 'Inbox' }));
  } else {
    const parts = pathOf(view);
    const nodes = [];
    parts.forEach((c, i) => {
      if (i) nodes.push(el('span', { className: 'sep', textContent: '/', ariaHidden: 'true' }));
      if (i === parts.length - 1) nodes.push(el('span', { className: 'here', textContent: c.name }));
      else {
        const b = el('button', { textContent: c.name });
        b.addEventListener('click', () => go(c.id));
        nodes.push(b);
      }
    });
    crumbs.replaceChildren(...nodes);
  }

  const open = openIn(view);
  $('#open-count').textContent = open ? `${open} open` : '';
  $('#cat-menu-btn').hidden = typeof view !== 'number';
  const cat = findCat(view);
  $('#new-title').placeholder = cat ? `Add to ${cat.name}` : "What's next?";
}

function renderTodos() {
  const inThisView = todos.filter((t) => inView(t));
  const open = inThisView.filter((t) => !t.done);
  const done = inThisView.filter((t) => t.done).reverse();

  // Group open todos: the viewed category's own todos first, then one group
  // per subcategory that has any.
  const groups = [];
  const direct = (catId) => open.filter((t) => (t.categoryId ?? null) === catId);
  if (view === ALL) {
    groups.push({ id: null, todos: direct(null) });
    for (const { cat } of flatten()) groups.push({ id: cat.id, title: pathName(cat.id), todos: direct(cat.id) });
  } else if (view === INBOX) {
    groups.push({ id: null, todos: open });
  } else {
    groups.push({ id: view, todos: direct(view) });
    for (const { cat } of flatten(view)) groups.push({ id: cat.id, title: pathName(cat.id, view), todos: direct(cat.id) });
  }

  $('#open-groups').replaceChildren(...groups.filter((g) => g.todos.length).map((g) => {
    const section = el('section', { className: 'group' });
    if (g.title) {
      const link = el('button', { textContent: g.title });
      link.addEventListener('click', () => go(g.id));
      section.append(el('h2', { className: 'group-title' }, link));
    }
    section.append(el('ul', { className: 'list' }, ...g.todos.map((t) => renderItem(t, false))));
    return section;
  }));

  const empty = $('#empty');
  empty.hidden = open.length > 0;
  empty.textContent = view === ALL && !todos.length
    ? 'Nothing here yet. Type above and press Enter to add your first todo.'
    : 'Nothing open here. Type above and press Enter to add one.';

  $('#done-section').hidden = done.length === 0;
  $('#done-count').textContent = `${done.length} done`;
  $('#done-toggle').setAttribute('aria-expanded', String(showDone));
  $('#done-list').hidden = !showDone;
  $('#done-list').replaceChildren(...done.map((t) => renderItem(t, true)));
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
  title.title = 'Added ' + new Date(todo.createdAt * 1000).toLocaleString();
  title.addEventListener('dblclick', () => startEdit(title, todo));
  title.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') startEdit(title, todo);
  });

  // Done todos are listed flat, so say where each one lives.
  if (showTag && todo.categoryId != null && todo.categoryId !== view) {
    $('.tag', li).textContent = pathName(todo.categoryId, typeof view === 'number' ? view : null);
  }

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
  if (dragging.kind === 'todo') return v !== ALL;
  // Categories go to the top level (All) or into a category outside themselves.
  if (v === INBOX) return false;
  return v === ALL || !isWithin(v, dragging.id);
}

function dropOn(v) {
  const d = dragging;
  dragging = null;
  if (!d) return;
  if (d.kind === 'todo') {
    run(() => api('PATCH', `/todos/${d.id}`, { categoryId: v === INBOX ? null : v }).then(fetchAll));
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
  if (!action || !cat) return;

  if (action === 'add-sub') return newCategory(cat.id);

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

// ── Other events ──

$('#add-form').addEventListener('submit', (e) => {
  e.preventDefault();
  const input = $('#new-title');
  const title = input.value.trim();
  if (!title) return;
  run(async () => {
    todos.push(await api('POST', '/todos', { title, categoryId: viewTarget() }));
    input.value = '';
  });
});

$('#new-category').addEventListener('click', () => newCategory(null));
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

$('#clear-done').addEventListener('click', () => run(async () => {
  await api('DELETE', '/todos/done');
  todos = todos.filter((t) => !t.done);
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
