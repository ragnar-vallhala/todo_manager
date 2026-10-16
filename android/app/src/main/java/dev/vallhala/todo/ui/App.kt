package dev.vallhala.todo.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Forum
import androidx.compose.material.icons.filled.Inbox
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.NavigationDrawerItemDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Category
import dev.vallhala.todo.Todo
import dev.vallhala.todo.View
import kotlinx.coroutines.launch

/** The dialog open on top of the main screen, if any. */
private sealed interface Dialog {
    data class NewCategory(val parentId: Int?) : Dialog
    data class Rename(val cat: Category) : Dialog
    data class Move(val cat: Category) : Dialog
    data class Style(val cat: Category) : Dialog
    data class DeleteCat(val cat: Category) : Dialog
    data object NewRoom : Dialog
    data class EditRoom(val id: Int) : Dialog
    data class DeleteRoom(val id: Int) : Dialog
    data object DeleteAllPruned : Dialog
    data object Theme : Dialog
    data object Settings : Dialog
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun App(model: AppModel) {
    if (model.needsSetup) {
        Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background))
        SettingsDialog(model, first = true, onDismiss = {})
        return
    }
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    val snackbar = remember { SnackbarHostState() }
    var dialog by remember { mutableStateOf<Dialog?>(null) }
    var openTodo by remember { mutableStateOf<Int?>(null) }

    LaunchedEffect(model.notices.size) {
        val msg = model.notices.firstOrNull() ?: return@LaunchedEffect
        model.notices.removeAt(0)
        snackbar.showSnackbar(msg)
    }

    BackHandler(enabled = drawer.isOpen) { scope.launch { drawer.close() } }
    BackHandler(enabled = !drawer.isOpen && model.view != View.All) { model.go(View.All) }

    ModalNavigationDrawer(
        drawerState = drawer,
        drawerContent = {
            ModalDrawerSheet {
                Drawer(model, onNewCategory = { dialog = Dialog.NewCategory(null) }, onNewRoom = { dialog = Dialog.NewRoom }) {
                    model.go(it)
                    scope.launch { drawer.close() }
                }
            }
        },
    ) {
        Scaffold(
            topBar = {
                CenterAlignedTopAppBar(
                    title = {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text(title(model), maxLines = 1, overflow = TextOverflow.Ellipsis)
                            model.offline?.let {
                                Text(it, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.error)
                            }
                        }
                    },
                    navigationIcon = {
                        IconButton(onClick = { scope.launch { drawer.open() } }) { Icon(Icons.Default.Menu, "Menu") }
                    },
                    actions = { Overflow(model) { dialog = it } },
                )
            },
            snackbarHost = { SnackbarHost(snackbar) },
            // Includes the keyboard, so the add / post boxes stay above it.
            contentWindowInsets = WindowInsets.safeDrawing,
        ) { padding ->
            val onOpen: (Todo) -> Unit = { openTodo = it.id }
            when (val v = model.view) {
                View.Pruned -> PrunedList(model, padding)
                is View.RoomView -> model.room(v.id)?.let { RoomScreen(model, it, padding, onOpen) }
                else -> TodoList(model, padding, Modifier.fillMaxSize(), onOpen)
            }
        }
    }

    // The sheet follows the todo as it reloads, and closes if it's gone.
    openTodo?.let { id ->
        val t = model.todos.find { it.id == id }
        if (t == null) openTodo = null else TodoSheet(model, t) { openTodo = null }
    }

    val close = { dialog = null }
    when (val d = dialog) {
        null -> {}
        is Dialog.NewCategory -> TextPrompt(
            if (d.parentId == null) "New category" else "New subcategory of ${model.pathName(d.parentId)}",
            label = "Name", confirm = "Create", onDismiss = close,
        ) { close(); model.addCategory(it, d.parentId) }
        is Dialog.Rename -> TextPrompt("Rename category", d.cat.name, "Name", onDismiss = close) {
            close(); model.renameCategory(d.cat.id, it)
        }
        is Dialog.Move -> CategoryPicker(model, "Move “${d.cat.name}” into", "Top level", d.cat.parentId, exclude = d.cat.id, onDismiss = close) {
            close(); model.moveCategory(d.cat.id, it)
        }
        is Dialog.Style -> StyleDialog(d.cat.name, d.cat.style, close) { close(); model.styleCategory(d.cat.id, it) }
        is Dialog.DeleteCat -> Confirm(
            "Delete “${d.cat.name}”?",
            "Its todos and subcategories move up to ${d.cat.parentId?.let { model.pathName(it) } ?: "the top level"}. No todos are deleted.",
            "Delete", close,
        ) { close(); model.deleteCategory(d.cat.id) }
        Dialog.NewRoom -> RoomDialog("New room", "", "", "Create", close) { n, t -> close(); model.addRoom(n, t) }
        is Dialog.EditRoom -> model.room(d.id)?.let { r ->
            RoomDialog("Edit room", r.name, r.topic, "Save", close) { n, t -> close(); model.updateRoom(r.id, n, t) }
        }
        is Dialog.DeleteRoom -> Confirm(
            "Delete room “${model.room(d.id)?.name}”?",
            "Its messages are deleted; its todos stay, without a room.",
            "Delete", close,
        ) { close(); model.deleteRoom(d.id) }
        Dialog.DeleteAllPruned -> Confirm("Delete all pruned todos?", "They can't be restored afterwards.", "Delete all", close) {
            close(); model.deleteAllPruned()
        }
        Dialog.Theme -> ThemeDialog(model.themes, model.themeName, close) { model.setTheme(it) }
        Dialog.Settings -> SettingsDialog(model, first = false, onDismiss = close)
    }
}

private fun title(model: AppModel) = when (val v = model.view) {
    View.All -> "All todos"
    View.Inbox -> "Inbox"
    View.Archive -> "Archived"
    View.Pruned -> "Pruned"
    is View.Cat -> model.pathName(v.id)
    is View.RoomView -> model.room(v.id)?.let { "# ${it.name}" } ?: "Room"
}

@Composable
private fun Overflow(model: AppModel, open: (Dialog) -> Unit) {
    var shown by remember { mutableStateOf(false) }
    IconButton(onClick = { shown = true }) { Icon(Icons.Default.MoreVert, "More") }
    DropdownMenu(shown, { shown = false }) {
        val item: @Composable (String, () -> Unit) -> Unit = { label, action ->
            DropdownMenuItem({ Text(label) }, { shown = false; action() })
        }
        when (val v = model.view) {
            is View.Cat -> model.cat(v.id)?.let { c ->
                item("New subcategory") { open(Dialog.NewCategory(c.id)) }
                item("Rename") { open(Dialog.Rename(c)) }
                item("Move") { open(Dialog.Move(c)) }
                item("Style…") { open(Dialog.Style(c)) }
                // Something inside an archived category comes back by unarchiving that one.
                if (c.archived) item("Unarchive") { model.archiveCategory(c.id, false) }
                else if (!model.inArchive(c.id)) item("Archive") { model.archiveCategory(c.id, true) }
                item("Delete") { open(Dialog.DeleteCat(c)) }
                HorizontalDivider()
            }
            is View.RoomView -> {
                item("Edit room") { open(Dialog.EditRoom(v.id)) }
                item("Delete room") { open(Dialog.DeleteRoom(v.id)) }
                HorizontalDivider()
            }
            View.Pruned -> {
                if (model.pruned.isNotEmpty()) item("Delete all pruned") { open(Dialog.DeleteAllPruned) }
                HorizontalDivider()
            }
            else -> {
                item("New category") { open(Dialog.NewCategory(null)) }
                HorizontalDivider()
            }
        }
        if (model.view != View.Pruned) item("Prune done") { model.pruneDone() }
        item("Refresh") { model.refresh() }
        item("Theme…") { open(Dialog.Theme) }
        item("Settings…") { open(Dialog.Settings) }
    }
}

// ── Drawer ──

@Composable
private fun Drawer(model: AppModel, onNewCategory: () -> Unit, onNewRoom: () -> Unit, go: (View) -> Unit) {
    val view = model.view
    val archiveOpen = view == View.Archive || (view is View.Cat && model.inArchive(view.id))
    LazyColumn(contentPadding = PaddingValues(12.dp)) {
        item {
            Column(Modifier.padding(start = 16.dp, top = 8.dp, bottom = 12.dp)) {
                Text("Todo", style = MaterialTheme.typography.titleLarge)
                Text("${model.agent} · ${model.server.removePrefix("http://")}", style = MaterialTheme.typography.bodySmall,
                    color = LocalExtra.current.muted, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        item { NavRow("All todos", Icons.AutoMirrored.Filled.List, view == View.All, model.openIn(View.All)) { go(View.All) } }
        item { NavRow("Inbox", Icons.Default.Inbox, view == View.Inbox, model.openIn(View.Inbox)) { go(View.Inbox) } }
        item { Section("Categories", onNewCategory) }
        items(model.flatten(null, skipCollapsed = true), key = { "c${it.first.id}" }) { (c, depth) ->
            CatRow(model, c, depth, view == View.Cat(c.id), go)
        }
        item { Section("Rooms", onNewRoom) }
        if (model.rooms.isEmpty()) item {
            Text("No rooms yet", Modifier.padding(start = 28.dp, bottom = 8.dp), color = LocalExtra.current.muted,
                style = MaterialTheme.typography.bodySmall)
        }
        items(model.rooms.sortedBy { it.name.lowercase() }, key = { "r${it.id}" }) { r ->
            NavRow(
                r.name, Icons.Default.Forum, view == View.RoomView(r.id),
                model.openIn(View.RoomView(r.id)),
                dot = model.unread(r) && view != View.RoomView(r.id),
            ) { go(View.RoomView(r.id)) }
        }
        item { HorizontalDivider(Modifier.padding(vertical = 8.dp)) }
        item { NavRow("Pruned", Icons.Default.ContentCut, view == View.Pruned, model.pruned.size) { go(View.Pruned) } }
        if (model.archiveRoots().isNotEmpty()) {
            item { NavRow("Archived", Icons.Default.Archive, view == View.Archive, model.openIn(View.Archive)) { go(View.Archive) } }
            if (archiveOpen) {
                val rows = model.archiveRoots().flatMap { root -> listOf(root to 1) + model.flatten(root.id).map { it.first to it.second + 2 } }
                items(rows, key = { "a${it.first.id}" }) { (c, depth) -> CatRow(model, c, depth, view == View.Cat(c.id), go, foldable = false) }
            }
        }
    }
}

@Composable
private fun Section(label: String, onAdd: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(start = 16.dp, top = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.labelLarge, color = LocalExtra.current.muted)
        IconButton(onClick = onAdd) { Icon(Icons.Default.Add, "New ${label.lowercase().removeSuffix("s")}") }
    }
}

@Composable
private fun NavRow(
    label: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    selected: Boolean,
    count: Int,
    dot: Boolean = false,
    onClick: () -> Unit,
) {
    NavigationDrawerItem(
        label = { Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        icon = { Icon(icon, null) },
        selected = selected,
        onClick = onClick,
        badge = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (dot) Box(Modifier.size(8.dp).background(LocalExtra.current.accent, CircleShape))
                if (count > 0) Text("  $count")
            }
        },
        modifier = Modifier.padding(NavigationDrawerItemDefaults.ItemPadding).height(44.dp),
    )
}

@Composable
private fun CatRow(model: AppModel, c: Category, depth: Int, selected: Boolean, go: (View) -> Unit, foldable: Boolean = true) {
    val st = model.styleOf(c.id)
    val bg = styleBg(st)
    val hasKids = foldable && model.children(c.id).any { !it.archived || model.inArchive(c.id) }
    val folded = c.id in model.collapsed
    NavigationDrawerItem(
        label = {
            Text(buildAnnotatedString { withStyle(styleSpan(st)) { append(c.name) } }, maxLines = 1, overflow = TextOverflow.Ellipsis)
        },
        icon = {
            if (hasKids) Icon(
                if (folded) Icons.Default.ChevronRight else Icons.Default.ExpandMore,
                if (folded) "Expand" else "Collapse",
                Modifier.clickable { model.toggleCollapsed(c.id) },
            ) else Icon(Icons.Default.Folder, null, tint = styleText(st) ?: LocalExtra.current.muted)
        },
        selected = selected,
        onClick = { go(View.Cat(c.id)) },
        badge = { model.openIn(View.Cat(c.id)).takeIf { it > 0 }?.let { Text("$it", color = styleText(st).takeIf { bg != null } ?: Color.Unspecified) } },
        colors = if (bg != null && !selected) NavigationDrawerItemDefaults.colors(unselectedContainerColor = bg) else NavigationDrawerItemDefaults.colors(),
        modifier = Modifier.padding(NavigationDrawerItemDefaults.ItemPadding).padding(start = (depth * 16).dp).height(40.dp),
    )
}

// ── Todo list ──

/** The open todos in the view, grouped by category, then the done ones, and a box to add more. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TodoList(model: AppModel, padding: PaddingValues, modifier: Modifier, onOpen: (Todo) -> Unit) {
    val groups = model.groups()
    val done = model.doneInView()
    val grouped = model.view == View.All || model.view == View.Archive || model.view is View.Cat
    Column(modifier.padding(padding)) {
        PullToRefreshBox(isRefreshing = model.loading, onRefresh = { model.refresh() }, modifier = Modifier.weight(1f)) {
            LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 16.dp)) {
                for (g in groups) {
                    if (g.title != null) item(key = "g${g.catId}") { GroupHeading(model, g.catId, g.title) }
                    items(g.todos, key = { it.id }) { TodoRow(model, it, showTag = !grouped, onOpen = { onOpen(it) }) }
                }
                if (groups.isEmpty()) item {
                    Text(
                        if (!model.loaded) (if (model.offline != null) "Can't reach the server. Pull down to retry, or check Settings." else "Loading…")
                        else emptyText(model),
                        Modifier.fillMaxWidth().padding(32.dp),
                        textAlign = TextAlign.Center,
                        color = LocalExtra.current.muted,
                    )
                }
                if (done.isNotEmpty()) {
                    item(key = "done") {
                        TextButton(onClick = { model.toggleShowDone() }, Modifier.padding(start = 8.dp, top = 8.dp)) {
                            Icon(if (model.showDone) Icons.Default.ExpandMore else Icons.Default.ChevronRight, null)
                            Text("${done.size} done")
                        }
                    }
                    if (model.showDone) items(done, key = { it.id }) { TodoRow(model, it, showTag = true, onOpen = { onOpen(it) }) }
                }
            }
        }
        if (model.view != View.Archive) AddBar(model)
    }
}

private fun emptyText(model: AppModel) = when (model.view) {
    is View.RoomView -> "No open todos in this room. Add one below."
    View.Archive -> "Nothing open in archived categories."
    View.All -> if (model.todos.isEmpty()) "Nothing here yet. Add your first todo below." else "Nothing open. Add one below."
    else -> "Nothing open here. Add one below."
}

@Composable
private fun GroupHeading(model: AppModel, catId: Int?, title: String) {
    val st = model.styleOf(catId)
    val bg = styleBg(st)
    Text(
        buildAnnotatedString {
            withStyle(styleSpan(st).copy(color = styleText(st) ?: LocalExtra.current.category, fontWeight = FontWeight.SemiBold)) { append(title) }
        },
        style = MaterialTheme.typography.titleSmall,
        modifier = Modifier.fillMaxWidth().padding(top = 12.dp)
            .then(if (bg != null) Modifier.background(bg) else Modifier)
            .clickable { catId?.let { model.go(View.Cat(it)) } }
            .padding(horizontal = 16.dp, vertical = 8.dp),
    )
}

@Composable
private fun AddBar(model: AppModel) {
    var text by remember { mutableStateOf("") }
    val target = when (val v = model.view) {
        is View.Cat -> model.cat(v.id)?.name
        is View.RoomView -> model.room(v.id)?.name
        else -> null
    }
    fun add() {
        if (text.isBlank()) return
        model.addTodo(text)
        text = ""
    }
    Surface(tonalElevation = 3.dp) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                text, { text = it },
                placeholder = { Text(target?.let { "Add to $it…" } ?: "Add a todo…") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { add() }),
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(4.dp))
            IconButton(onClick = ::add, enabled = text.isNotBlank()) { Icon(Icons.Default.Add, "Add") }
        }
    }
}

// ── Pruned ──

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PrunedList(model: AppModel, padding: PaddingValues) {
    val list = model.pruned.sortedWith(compareByDescending<Todo> { it.prunedAt ?: 0 }.thenByDescending { it.id })
    PullToRefreshBox(isRefreshing = model.loading, onRefresh = { model.refresh() }, modifier = Modifier.fillMaxSize().padding(padding)) {
        LazyColumn(Modifier.fillMaxSize()) {
            if (list.isEmpty()) item {
                Text("Nothing pruned. Prune done todos to free their ids.", Modifier.fillMaxWidth().padding(32.dp),
                    textAlign = TextAlign.Center, color = LocalExtra.current.muted)
            }
            items(list, key = { "p${it.id}" }) { t ->
                Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(t.title, style = MaterialTheme.typography.bodyLarge, color = LocalExtra.current.muted)
                        Text(
                            "p${t.id}" + (t.prunedAt?.let { " · pruned ${ago(it)}" } ?: "") + (t.categoryId?.let { " · ${model.pathName(it)}" } ?: ""),
                            style = MaterialTheme.typography.labelMedium, color = LocalExtra.current.muted,
                        )
                    }
                    IconButton(onClick = { model.restore(t.id) }) { Icon(Icons.AutoMirrored.Filled.Undo, "Restore") }
                    IconButton(onClick = { model.deletePruned(t.id) }) { Icon(Icons.Default.Delete, "Delete for good", tint = MaterialTheme.colorScheme.error) }
                }
            }
        }
    }
}
