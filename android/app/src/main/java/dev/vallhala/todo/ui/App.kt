package dev.vallhala.todo.ui

import android.Manifest
import android.os.Build
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AddComment
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Forum
import androidx.compose.material.icons.filled.Inbox
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.TaskAlt
import androidx.compose.material.icons.outlined.Forum
import androidx.compose.material.icons.outlined.TaskAlt
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.NavigationDrawerItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.rememberDrawerState
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Category
import dev.vallhala.todo.Notify
import dev.vallhala.todo.Screen
import dev.vallhala.todo.Todo
import dev.vallhala.todo.View
import kotlinx.coroutines.launch

/** The dialog open on top of everything, if any. */
sealed interface AppDialog {
    data class NewCategory(val parentId: Int?) : AppDialog
    data class Rename(val cat: Category) : AppDialog
    data class Move(val cat: Category) : AppDialog
    data class Style(val cat: Category) : AppDialog
    data class DeleteCat(val cat: Category) : AppDialog
    data object NewRoom : AppDialog
    data class EditRoom(val id: Int) : AppDialog
    data class DeleteRoom(val id: Int) : AppDialog
    data object DeleteAllPruned : AppDialog
    data class PruneMessages(val roomId: Int) : AppDialog
    data class DeleteAllPrunedMessages(val roomId: Int) : AppDialog
    data object Theme : AppDialog
    data object Settings : AppDialog
    data object Usage : AppDialog
}

@Composable
fun App(model: AppModel) {
    if (model.needsSetup) {
        Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background))
        SettingsDialog(model, first = true, onDismiss = {})
        return
    }
    val snackbar = remember { SnackbarHostState() }
    var dialog by remember { mutableStateOf<AppDialog?>(null) }
    var openTodo by remember { mutableStateOf<Int?>(null) }
    val open: (AppDialog) -> Unit = { dialog = it }
    val onOpen: (Todo) -> Unit = { openTodo = it.id }

    LaunchedEffect(model.notices.size) {
        val msg = model.notices.firstOrNull() ?: return@LaunchedEffect
        model.notices.removeAt(0)
        snackbar.showSnackbar(msg)
    }

    // Ask once (Android 13+) whether we may notify about @mentions.
    val ctx = LocalContext.current
    val askNotify = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {}
    LaunchedEffect(Unit) {
        if (Build.VERSION.SDK_INT >= 33 && !model.prefs.askedNotify && !Notify.allowed(ctx)) {
            model.prefs.askedNotify = true
            askNotify.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    BackHandler(enabled = model.stack.isNotEmpty()) {
        if (model.stack.last() == Screen.Share) model.cancelShare() else model.pop()
    }

    // Each screen keeps its scroll position and typing while others are on top of it.
    val holder = rememberSaveableStateHolder()
    val top = model.stack.lastOrNull()
    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
        AnimatedContent(
            targetState = top to model.stack.size,
            contentKey = { it.first?.key ?: "home" },
            transitionSpec = {
                val forward = targetState.second >= initialState.second
                val dur = 280
                if (forward) {
                    (slideInHorizontally(tween(dur)) { it / 3 } + fadeIn(tween(dur))) togetherWith
                        (slideOutHorizontally(tween(dur)) { -it / 6 } + fadeOut(tween(dur / 2)))
                } else {
                    (slideInHorizontally(tween(dur)) { -it / 6 } + fadeIn(tween(dur))) togetherWith
                        (slideOutHorizontally(tween(dur)) { it / 3 } + fadeOut(tween(dur / 2)))
                }
            },
            label = "screens",
        ) { (s, _) ->
            holder.SaveableStateProvider(s?.key ?: "home") {
                when (s) {
                    null -> Home(model, open, onOpen)
                    is Screen.Chat -> model.room(s.roomId)?.let { ChatScreen(model, it, open) { id -> openTodo = id } }
                    is Screen.RoomTodos -> RoomTodosScreen(model, s.roomId, onOpen)
                    is Screen.RoomPruned -> model.room(s.roomId)?.let { PrunedMessagesScreen(model, it, open) }
                    is Screen.RoomMedia -> model.room(s.roomId)?.let { RoomMediaScreen(model, it) }
                    Screen.Share -> SharePicker(model)
                }
            }
        }
        // Above the bottom bar on the home screen, above the message box in a room.
        val chat = top is Screen.Chat
        SnackbarHost(
            snackbar,
            Modifier.align(Alignment.BottomCenter)
                .windowInsetsPadding(WindowInsets.navigationBars.union(WindowInsets.ime))
                .padding(bottom = if (chat) 64.dp else if (top == null) 84.dp else 8.dp),
        )
    }

    // The sheet follows the todo as it reloads, and closes if it's gone.
    openTodo?.let { id ->
        val t = model.todos.find { it.id == id }
        if (t == null) openTodo = null else TodoSheet(model, t) { openTodo = null }
    }

    model.gallery?.let { ImageViewer(model, it) }

    val close = { dialog = null }
    when (val d = dialog) {
        null -> {}
        is AppDialog.NewCategory -> TextPrompt(
            if (d.parentId == null) "New category" else "New subcategory of ${model.pathName(d.parentId)}",
            label = "Name", confirm = "Create", onDismiss = close,
        ) { close(); model.addCategory(it, d.parentId) }
        is AppDialog.Rename -> TextPrompt("Rename category", d.cat.name, "Name", onDismiss = close) {
            close(); model.renameCategory(d.cat.id, it)
        }
        is AppDialog.Move -> CategoryPicker(model, "Move “${d.cat.name}” into", "Top level", d.cat.parentId, exclude = d.cat.id, onDismiss = close) {
            close(); model.moveCategory(d.cat.id, it)
        }
        is AppDialog.Style -> StyleDialog(d.cat.name, d.cat.style, close) { close(); model.styleCategory(d.cat.id, it) }
        is AppDialog.DeleteCat -> Confirm(
            "Delete “${d.cat.name}”?",
            "Its todos and subcategories move up to ${d.cat.parentId?.let { model.pathName(it) } ?: "the top level"}. No todos are deleted.",
            "Delete", close,
        ) { close(); model.deleteCategory(d.cat.id) }
        AppDialog.NewRoom -> RoomDialog("New room", "", "", "Create", close) { n, t -> close(); model.addRoom(n, t) }
        is AppDialog.EditRoom -> model.room(d.id)?.let { r ->
            RoomDialog("Edit room", r.name, r.topic, "Save", close) { n, t -> close(); model.updateRoom(r.id, n, t) }
        }
        is AppDialog.DeleteRoom -> Confirm(
            "Delete room “${model.room(d.id)?.name}”?",
            "Its messages are deleted; its todos stay, without a room.",
            "Delete", close,
        ) { close(); model.deleteRoom(d.id) }
        AppDialog.DeleteAllPruned -> Confirm("Delete all pruned todos?", "They can't be restored afterwards.", "Delete all", close) {
            close(); model.deleteAllPruned()
        }
        is AppDialog.PruneMessages -> Confirm(
            "Prune the messages in “${model.room(d.roomId)?.name}”?",
            "They move to the room's pruned messages, freeing their ids; you can restore them from there.",
            "Prune", close,
        ) { close(); model.pruneMessages(d.roomId) }
        is AppDialog.DeleteAllPrunedMessages -> Confirm(
            "Delete the pruned messages in “${model.room(d.roomId)?.name}”?",
            "They can't be restored afterwards.", "Delete all", close,
        ) { close(); model.deleteAllPrunedMessages(d.roomId) }
        AppDialog.Theme -> ThemeDialog(model.themes, model.themeName, close) { model.setTheme(it) }
        AppDialog.Settings -> SettingsDialog(model, first = false, onDismiss = close)
        AppDialog.Usage -> UsageDialog(pollUsage(model).value, close)
    }
}

// ── Home: todos and chats ──

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun Home(model: AppModel, open: (AppDialog) -> Unit, onOpen: (Todo) -> Unit) {
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    val tab = model.homeTab
    var searching by rememberSaveable { mutableStateOf(false) }
    var query by rememberSaveable { mutableStateOf("") }
    var adding by remember { mutableStateOf(false) }
    val usage by pollUsage(model)
    val todoList = rememberLazyListState()
    val barState = rememberTopAppBarState()
    val scroll = TopAppBarDefaults.enterAlwaysScrollBehavior(barState)

    BackHandler(enabled = drawer.isOpen) { scope.launch { drawer.close() } }
    BackHandler(enabled = !drawer.isOpen && searching) { searching = false; query = "" }
    BackHandler(enabled = !drawer.isOpen && tab == 0 && model.view != View.All) { model.go(View.All) }

    ModalNavigationDrawer(
        drawerState = drawer,
        gesturesEnabled = tab == 0 || drawer.isOpen,
        drawerContent = {
            ModalDrawerSheet {
                Drawer(model, onNewCategory = { open(AppDialog.NewCategory(null)) }, onNewRoom = { open(AppDialog.NewRoom) }) {
                    model.go(it)
                    model.setTab(0)
                    scope.launch { drawer.close() }
                }
            }
        },
    ) {
        Scaffold(
            modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
            topBar = {
                if (tab == 0) TopAppBar(
                    title = {
                        Column {
                            Text(title(model), maxLines = 1, overflow = TextOverflow.Ellipsis)
                            OfflineLine(model)
                            UsageStrip(usage)
                        }
                    },
                    navigationIcon = { IconButton(onClick = { scope.launch { drawer.open() } }) { Icon(Icons.Default.Menu, "Menu") } },
                    actions = { TodoOverflow(model, open) },
                    scrollBehavior = scroll,
                ) else TopAppBar(
                    title = {
                        if (searching) SearchField(query) { query = it }
                        else Column {
                            Text("Chats")
                            OfflineLine(model)
                            UsageStrip(usage)
                        }
                    },
                    navigationIcon = {
                        if (searching) IconButton(onClick = { searching = false; query = "" }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Close search") }
                    },
                    actions = {
                        if (!searching) IconButton(onClick = { searching = true }) { Icon(Icons.Default.Search, "Search rooms") }
                        else if (query.isNotEmpty()) IconButton(onClick = { query = "" }) { Icon(Icons.Default.Close, "Clear") }
                        ChatOverflow(model, open)
                    },
                    scrollBehavior = scroll,
                )
            },
            bottomBar = {
                NavigationBar {
                    val openTodos = model.openIn(View.All)
                    NavigationBarItem(
                        selected = tab == 0,
                        onClick = { model.setTab(0) },
                        icon = {
                            BadgedBox(badge = { if (openTodos > 0) Badge(containerColor = MaterialTheme.colorScheme.secondary) { Text("$openTodos") } }) {
                                Icon(if (tab == 0) Icons.Default.TaskAlt else Icons.Outlined.TaskAlt, null)
                            }
                        },
                        label = { Text("Todos") },
                    )
                    val unreadRooms = model.rooms.count { model.unread(it) > 0 }
                    NavigationBarItem(
                        selected = tab == 1,
                        onClick = { model.setTab(1) },
                        icon = {
                            BadgedBox(badge = { if (unreadRooms > 0) Badge { Text("$unreadRooms") } }) {
                                Icon(if (tab == 1) Icons.Default.Forum else Icons.Outlined.Forum, null)
                            }
                        },
                        label = { Text("Chats") },
                    )
                }
            },
            floatingActionButton = {
                when {
                    tab == 1 -> FloatingActionButton(onClick = { open(AppDialog.NewRoom) }) { Icon(Icons.Default.AddComment, "New room") }
                    model.view != View.Pruned && model.view != View.Archive -> NewTodoFab(todoList) { adding = true }
                }
            },
        ) { padding ->
            AnimatedContent(tab, transitionSpec = { fadeIn(tween(180)) togetherWith fadeOut(tween(120)) }, label = "tab") { t ->
                if (t == 1) ChatList(model, padding, query, open)
                else if (model.view == View.Pruned) PrunedList(model, padding)
                else TodoList(model, model.view, padding, onOpen, todoList)
            }
        }
    }
    if (adding) AddTodoSheet(model, model.view) { adding = false }
}

@Composable
private fun OfflineLine(model: AppModel) {
    AnimatedVisibility(model.offline != null) {
        Text(model.offline ?: "", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.error, maxLines = 1)
    }
}

@Composable
private fun SearchField(query: String, onChange: (String) -> Unit) {
    val focus = remember { FocusRequester() }
    TextField(
        query, onChange,
        placeholder = { Text("Search rooms") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
        colors = TextFieldDefaults.colors(
            focusedContainerColor = Color.Transparent,
            unfocusedContainerColor = Color.Transparent,
            focusedIndicatorColor = Color.Transparent,
            unfocusedIndicatorColor = Color.Transparent,
        ),
        modifier = Modifier.fillMaxWidth().focusRequester(focus),
    )
    LaunchedEffect(Unit) { focus.requestFocus() }
}

private fun title(model: AppModel) = when (val v = model.view) {
    View.All -> "All todos"
    View.Inbox -> "Inbox"
    View.Archive -> "Archived"
    View.Pruned -> "Pruned"
    is View.Cat -> model.pathName(v.id)
    is View.RoomView -> model.room(v.id)?.let { "${it.name} todos" } ?: "Room"
}

@Composable
private fun TodoOverflow(model: AppModel, open: (AppDialog) -> Unit) {
    var shown by remember { mutableStateOf(false) }
    IconButton(onClick = { shown = true }) { Icon(Icons.Default.MoreVert, "More") }
    DropdownMenu(shown, { shown = false }) {
        val item: @Composable (String, () -> Unit) -> Unit = { label, action ->
            DropdownMenuItem({ Text(label) }, { shown = false; action() })
        }
        when (val v = model.view) {
            is View.Cat -> model.cat(v.id)?.let { c ->
                item("New subcategory") { open(AppDialog.NewCategory(c.id)) }
                item("Rename") { open(AppDialog.Rename(c)) }
                item("Move") { open(AppDialog.Move(c)) }
                item("Style…") { open(AppDialog.Style(c)) }
                // Something inside an archived category comes back by unarchiving that one.
                if (c.archived) item("Unarchive") { model.archiveCategory(c.id, false) }
                else if (!model.inArchive(c.id)) item("Archive") { model.archiveCategory(c.id, true) }
                item("Delete") { open(AppDialog.DeleteCat(c)) }
                HorizontalDivider()
            }
            is View.RoomView -> {
                item("Open chat") { model.openChat(v.id) }
                HorizontalDivider()
            }
            View.Pruned -> {
                if (model.pruned.isNotEmpty()) item("Delete all pruned") { open(AppDialog.DeleteAllPruned) }
                HorizontalDivider()
            }
            else -> {
                item("New category") { open(AppDialog.NewCategory(null)) }
                HorizontalDivider()
            }
        }
        if (model.view != View.Pruned) item("Prune done") { model.pruneDone() }
        item("Refresh") { model.refresh() }
        item("Claude usage…") { open(AppDialog.Usage) }
        item("Theme…") { open(AppDialog.Theme) }
        item("Settings…") { open(AppDialog.Settings) }
    }
}

@Composable
private fun ChatOverflow(model: AppModel, open: (AppDialog) -> Unit) {
    var shown by remember { mutableStateOf(false) }
    IconButton(onClick = { shown = true }) { Icon(Icons.Default.MoreVert, "More") }
    DropdownMenu(shown, { shown = false }) {
        DropdownMenuItem({ Text("New room") }, { shown = false; open(AppDialog.NewRoom) })
        DropdownMenuItem({ Text("Mark all as read") }, { shown = false; model.rooms.forEach { if (model.unread(it) > 0) model.markRead(it) } })
        DropdownMenuItem({ Text("Refresh") }, { shown = false; model.refresh() })
        HorizontalDivider()
        DropdownMenuItem({ Text("Claude usage…") }, { shown = false; open(AppDialog.Usage) })
        DropdownMenuItem({ Text("Theme…") }, { shown = false; open(AppDialog.Theme) })
        DropdownMenuItem({ Text("Settings…") }, { shown = false; open(AppDialog.Settings) })
    }
}

// ── Drawer ──

@Composable
private fun Drawer(model: AppModel, onNewCategory: () -> Unit, onNewRoom: () -> Unit, go: (View) -> Unit) {
    val view = model.view
    val archiveOpen = view == View.Archive || (view is View.Cat && model.inArchive(view.id))
    LazyColumn(contentPadding = PaddingValues(12.dp)) {
        item {
            Row(Modifier.padding(start = 12.dp, top = 12.dp, bottom = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                Avatar(model.agent, 44.dp)
                Spacer(Modifier.width(12.dp))
                Column {
                    Text(model.agent, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(model.server.removePrefix("http://"), style = MaterialTheme.typography.bodySmall,
                        color = LocalExtra.current.muted, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
        }
        item { NavRow("All todos", Icons.AutoMirrored.Filled.List, view == View.All, model.openIn(View.All)) { go(View.All) } }
        item { NavRow("Inbox", Icons.Default.Inbox, view == View.Inbox, model.openIn(View.Inbox)) { go(View.Inbox) } }
        item { Section("Categories", onNewCategory) }
        items(model.flatten(null, skipCollapsed = true), key = { "c${it.first.id}" }) { (c, depth) ->
            CatRow(model, c, depth, view == View.Cat(c.id), go)
        }
        item { Section("Room todos", onNewRoom) }
        if (model.rooms.isEmpty()) item {
            Text("No rooms yet", Modifier.padding(start = 28.dp, bottom = 8.dp), color = LocalExtra.current.muted,
                style = MaterialTheme.typography.bodySmall)
        }
        items(model.rooms.sortedBy { it.name.lowercase() }, key = { "r${it.id}" }) { r ->
            NavRow(r.name, Icons.Default.Forum, view == View.RoomView(r.id), model.openIn(View.RoomView(r.id))) { go(View.RoomView(r.id)) }
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
private fun NavRow(label: String, icon: ImageVector, selected: Boolean, count: Int, onClick: () -> Unit) {
    NavigationDrawerItem(
        label = { Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        icon = { Icon(icon, null) },
        selected = selected,
        onClick = onClick,
        badge = { if (count > 0) Text("$count") },
        modifier = Modifier.padding(NavigationDrawerItemDefaults.ItemPadding).height(48.dp),
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
                Modifier.size(24.dp).clickable { model.toggleCollapsed(c.id) },
            ) else Icon(Icons.Default.Folder, null, tint = styleText(st) ?: LocalExtra.current.muted)
        },
        selected = selected,
        onClick = { go(View.Cat(c.id)) },
        badge = { model.openIn(View.Cat(c.id)).takeIf { it > 0 }?.let { Text("$it", color = styleText(st).takeIf { bg != null } ?: Color.Unspecified) } },
        colors = if (bg != null && !selected) NavigationDrawerItemDefaults.colors(unselectedContainerColor = bg) else NavigationDrawerItemDefaults.colors(),
        modifier = Modifier.padding(NavigationDrawerItemDefaults.ItemPadding).padding(start = (depth * 16).dp).height(44.dp),
    )
}

// ── Pruned todos ──

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PrunedList(model: AppModel, padding: PaddingValues) {
    val list = model.pruned.sortedWith(compareByDescending<Todo> { it.prunedAt ?: 0 }.thenByDescending { it.id })
    PullToRefreshBox(isRefreshing = model.loading, onRefresh = { model.refresh() }, modifier = Modifier.fillMaxSize().padding(padding)) {
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(vertical = 4.dp)) {
            if (list.isEmpty()) item {
                EmptyState(Icons.Default.ContentCut, "Nothing pruned", "Prune done todos to free their ids. They wait here to be restored.")
            }
            items(list, key = { "p${it.id}" }) { t ->
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 3.dp)
                        .background(MaterialTheme.colorScheme.surfaceContainerLow, RoundedCornerShape(16.dp))
                        .padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
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
