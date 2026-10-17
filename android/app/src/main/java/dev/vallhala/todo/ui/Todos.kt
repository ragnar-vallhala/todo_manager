package dev.vallhala.todo.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Notes
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Event
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.Flag
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Forum
import androidx.compose.material.icons.filled.Inventory2
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.TaskAlt
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DatePicker
import androidx.compose.material3.DatePickerDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.rememberDatePickerState
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.GalleryItem
import dev.vallhala.todo.Todo
import dev.vallhala.todo.View
import dev.vallhala.todo.sizeLabel
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset

// ── Priority colors ──

@Composable
private fun priorityColor(p: String?): Color = when (p) {
    "high" -> LocalExtra.current.danger
    "medium" -> LocalExtra.current.warn
    "low" -> MaterialTheme.colorScheme.tertiary
    else -> MaterialTheme.colorScheme.outline
}

// ── The list ──

/** The open todos in [view], grouped by category, then the done ones. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TodoList(
    model: AppModel,
    view: View,
    padding: PaddingValues,
    onOpen: (Todo) -> Unit,
    state: LazyListState = rememberLazyListState(),
) {
    val groups = model.groups(view)
    val done = model.doneInView(view)
    val grouped = view == View.All || view == View.Archive || view is View.Cat
    PullToRefreshBox(isRefreshing = model.loading, onRefresh = { model.refresh() }, modifier = Modifier.fillMaxSize().padding(padding)) {
        LazyColumn(Modifier.fillMaxSize(), state = state, contentPadding = PaddingValues(top = 4.dp, bottom = 96.dp)) {
            for (g in groups) {
                if (g.title != null) item(key = "g${g.catId}") { GroupHeading(model, g.catId, g.title, g.todos.size) }
                items(g.todos, key = { it.id }) {
                    SwipeTodo(model, it, showTag = !grouped, view = view, onOpen = { onOpen(it) }, modifier = Modifier.animateItem())
                }
            }
            if (groups.isEmpty()) item(key = "empty") {
                when {
                    !model.loaded && model.offline != null -> EmptyState(Icons.Default.Inventory2, "Can't reach the server", "Pull down to retry, or check Settings.")
                    !model.loaded -> Box(Modifier.fillMaxWidth().padding(48.dp), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
                    else -> EmptyState(Icons.Default.TaskAlt, "All clear", emptyText(model, view))
                }
            }
            if (done.isNotEmpty()) {
                item(key = "done") {
                    TextButton(onClick = { model.toggleShowDone() }, Modifier.padding(start = 8.dp, top = 8.dp)) {
                        Icon(if (model.showDone) Icons.Default.ExpandMore else Icons.Default.ChevronRight, null)
                        Spacer(Modifier.width(4.dp))
                        Text("${done.size} done")
                    }
                }
                if (model.showDone) items(done, key = { it.id }) {
                    SwipeTodo(model, it, showTag = true, view = view, onOpen = { onOpen(it) }, modifier = Modifier.animateItem())
                }
            }
        }
    }
}

private fun emptyText(model: AppModel, view: View) = when (view) {
    is View.RoomView -> "No open todos in this room. Add one with the button below."
    View.Archive -> "Nothing open in archived categories."
    View.All -> if (model.todos.isEmpty()) "Nothing here yet. Add your first todo." else "Nothing open. Enjoy it, or add another."
    else -> "Nothing open here."
}

@Composable
private fun GroupHeading(model: AppModel, catId: Int?, title: String, count: Int) {
    val st = model.styleOf(catId)
    val bg = styleBg(st)
    Row(
        Modifier.fillMaxWidth().padding(start = 12.dp, end = 12.dp, top = 14.dp, bottom = 2.dp)
            .clip(RoundedCornerShape(10.dp))
            .then(if (bg != null) Modifier.background(bg) else Modifier)
            .clickable { catId?.let { model.go(View.Cat(it)) } }
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Icons.Default.Folder, null, Modifier.size(16.dp), tint = styleText(st) ?: LocalExtra.current.category)
        Spacer(Modifier.width(8.dp))
        Text(
            androidx.compose.ui.text.buildAnnotatedString {
                pushStyle(styleSpan(st).copy(color = styleText(st) ?: LocalExtra.current.category))
                append(title)
                pop()
            },
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text("$count", style = MaterialTheme.typography.labelMedium, color = styleText(st)?.copy(alpha = 0.7f) ?: LocalExtra.current.muted)
    }
}

/**
 * A todo you can swipe: right to tick it done (or undo), left to claim it
 * (or release it if it's yours). The row springs back; the change shows in it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SwipeTodo(model: AppModel, t: Todo, showTag: Boolean, view: View, onOpen: () -> Unit, modifier: Modifier = Modifier) {
    val haptic = LocalHapticFeedback.current
    val mine = t.claimedBy == model.agent
    val state = rememberSwipeToDismissBoxState(
        confirmValueChange = { v ->
            when (v) {
                SwipeToDismissBoxValue.StartToEnd -> {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    model.setDone(t, !t.done)
                }
                SwipeToDismissBoxValue.EndToStart -> {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    when {
                        t.claimedBy == null -> model.claim(t)
                        mine -> model.release(t)
                        else -> model.notice("${t.claimedBy} has #${t.id}; open it to take over")
                    }
                }
                SwipeToDismissBoxValue.Settled -> {}
            }
            false
        },
        positionalThreshold = { it * 0.3f },
    )
    SwipeToDismissBox(
        state = state,
        modifier = modifier,
        enableDismissFromEndToStart = !t.done,
        backgroundContent = {
            val dir = state.dismissDirection
            val extra = LocalExtra.current
            val (color, icon, label) = when (dir) {
                SwipeToDismissBoxValue.StartToEnd -> Triple(if (t.done) extra.warn else extra.done, if (t.done) Icons.AutoMirrored.Filled.Undo else Icons.Default.Check, if (t.done) "Not done" else "Done")
                SwipeToDismissBoxValue.EndToStart -> Triple(MaterialTheme.colorScheme.primary, Icons.Default.Flag, when {
                    t.claimedBy == null -> "Claim"; mine -> "Release"; else -> "Taken"
                })
                else -> Triple(Color.Transparent, null, "")
            }
            val progress = state.progress.takeIf { dir != SwipeToDismissBoxValue.Settled } ?: 0f
            val scale by animateFloatAsState(if (progress > 0.3f) 1.15f else 0.9f, label = "swipeIcon")
            Box(
                Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 3.dp).clip(RoundedCornerShape(16.dp)).background(color)
                    .padding(horizontal = 22.dp),
                contentAlignment = if (dir == SwipeToDismissBoxValue.StartToEnd) Alignment.CenterStart else Alignment.CenterEnd,
            ) {
                if (icon != null) Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(icon, null, Modifier.scale(scale), tint = Color.White)
                    Spacer(Modifier.width(8.dp))
                    Text(label, color = Color.White, style = MaterialTheme.typography.labelLarge)
                }
            }
        },
    ) {
        TodoRow(model, t, showTag, view, onOpen)
    }
}

/**
 * One todo as a card: round tick in its priority's color, title, and chips
 * for what's set (priority, deadline, claim, room, category, notes, files).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TodoRow(model: AppModel, t: Todo, showTag: Boolean, view: View, onOpen: () -> Unit) {
    val extra = LocalExtra.current
    val st = model.styleOf(t.categoryId)
    val bg = styleBg(st)
    val text = styleText(st)?.takeIf { bg != null } ?: MaterialTheme.colorScheme.onSurface
    val muted = if (bg != null) text.copy(alpha = 0.72f) else extra.muted
    Surface(
        onClick = onOpen,
        shape = RoundedCornerShape(16.dp),
        color = bg ?: MaterialTheme.colorScheme.surfaceContainerLow,
        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 3.dp),
    ) {
        Row(Modifier.padding(start = 6.dp, end = 14.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Tick(t.done, priorityColor(t.priority)) { model.setDone(t, !t.done) }
            Column(Modifier.weight(1f).padding(vertical = 6.dp)) {
                Text(
                    t.title,
                    color = if (t.done) muted else text,
                    textDecoration = if (t.done) TextDecoration.LineThrough else null,
                    style = MaterialTheme.typography.bodyLarge,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                )
                val tags = showTag && t.categoryId != null
                val inRoom = t.roomId != null && view !is View.RoomView
                val hasMeta = t.priority != null || t.deadline != null || t.description != null || t.claimedBy != null ||
                    tags || inRoom || t.attachments.isNotEmpty()
                if (hasMeta) FlowRow(
                    Modifier.padding(top = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    t.priority?.let {
                        Chip(null, it.replaceFirstChar { c -> c.uppercase() }, if (bg != null) text else priorityColor(it), strong = true)
                    }
                    t.deadline?.let {
                        val late = overdue(it) && !t.done
                        Chip(Icons.Default.Event, dueLabel(it), if (late) extra.danger else if (bg != null) text else MaterialTheme.colorScheme.secondary, strong = late)
                    }
                    t.claimedBy?.let {
                        Chip(Icons.Default.Flag, if (it == model.agent) "you" else it, if (bg != null) text else extra.accent)
                    }
                    if (inRoom) Chip(Icons.Default.Forum, model.room(t.roomId)?.name ?: "room ${t.roomId}", muted)
                    if (tags) {
                        val cst = model.styleOf(t.categoryId)
                        Chip(Icons.Default.Folder, model.pathName(t.categoryId), if (bg == null) styleText(cst) ?: extra.category else muted)
                    }
                    if (t.attachments.isNotEmpty()) Chip(Icons.Default.AttachFile, "${t.attachments.size}", muted)
                    if (t.description != null) Icon(Icons.AutoMirrored.Filled.Notes, "Has notes", Modifier.size(16.dp).align(Alignment.CenterVertically), tint = muted)
                }
            }
            Text("#${t.id}", style = MaterialTheme.typography.labelSmall, color = muted)
        }
    }
}

/** A round check box, ringed in [color] (the priority's), filled when done. */
@Composable
private fun Tick(done: Boolean, color: Color, onClick: () -> Unit) {
    val fill by animateColorAsState(if (done) LocalExtra.current.done else Color.Transparent, label = "tick")
    Box(
        Modifier.size(44.dp).clip(CircleShape).clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.size(22.dp).clip(CircleShape).background(fill)
                .border(BorderStroke(2.dp, if (done) LocalExtra.current.done else color), CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            AnimatedVisibility(done, enter = scaleIn() + fadeIn(), exit = scaleOut() + fadeOut()) {
                Icon(Icons.Default.Check, "Done", Modifier.size(15.dp), tint = Color.White)
            }
        }
    }
}

/** A small tinted pill: an icon and a label in [color]. */
@Composable
private fun Chip(icon: ImageVector?, label: String, color: Color, strong: Boolean = false) {
    Row(
        Modifier.clip(RoundedCornerShape(8.dp)).background(color.copy(alpha = if (strong) 0.16f else 0.10f))
            .padding(horizontal = 7.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            Icon(icon, null, Modifier.size(13.dp), tint = color)
            Spacer(Modifier.width(3.dp))
        }
        Text(
            label,
            style = MaterialTheme.typography.labelMedium,
            color = color,
            fontWeight = if (strong) FontWeight.SemiBold else null,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/** The "New todo" button, shrinking to an icon while the list scrolls down. */
@Composable
fun NewTodoFab(state: LazyListState, onClick: () -> Unit) {
    val expanded by remember { derivedStateOf { state.firstVisibleItemIndex == 0 } }
    ExtendedFloatingActionButton(
        onClick = onClick,
        expanded = expanded,
        icon = { Icon(Icons.Default.Add, null) },
        text = { Text("New todo") },
    )
}

// ── Adding ──

/** A quick sheet for a new todo in [view]: title, and optionally notes, priority, deadline. */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun AddTodoSheet(model: AppModel, view: View, onDismiss: () -> Unit) {
    var title by remember { mutableStateOf("") }
    var notes by remember { mutableStateOf("") }
    var priority by remember { mutableStateOf<String?>(null) }
    var deadline by remember { mutableStateOf<String?>(null) }
    var pickDate by remember { mutableStateOf(false) }
    val focus = remember { FocusRequester() }
    val where = when (view) {
        is View.Cat -> model.cat(view.id)?.name
        is View.RoomView -> model.room(view.id)?.name
        else -> null
    }
    fun add() {
        if (title.isBlank()) return
        model.addTodo(title, priority, deadline, notes.ifBlank { null }, view)
        onDismiss()
    }
    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)) {
        Column(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp).imePadding().navigationBarsPadding(),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(if (where != null) "New todo in $where" else "New todo", style = MaterialTheme.typography.titleLarge)
            OutlinedTextField(
                title, { title = it },
                placeholder = { Text("What needs doing?") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { add() }),
                shape = RoundedCornerShape(14.dp),
                modifier = Modifier.fillMaxWidth().focusRequester(focus),
            )
            LaunchedEffect(Unit) { focus.requestFocus() }
            OutlinedTextField(
                notes, { notes = it },
                placeholder = { Text("Notes (optional)") },
                minLines = 2,
                shape = RoundedCornerShape(14.dp),
                modifier = Modifier.fillMaxWidth(),
            )
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                for ((value, label) in PRIORITY_NAMES.drop(1)) {
                    FilterChip(
                        priority == value, { priority = if (priority == value) null else value },
                        { Text(label) },
                        leadingIcon = { Box(Modifier.size(10.dp).clip(CircleShape).background(priorityColor(value))) },
                    )
                }
            }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                val today = LocalDate.now()
                FilterChip(deadline == today.toString(), { deadline = today.toString().takeIf { deadline != it } }, { Text("Today") })
                FilterChip(deadline == today.plusDays(1).toString(), { deadline = today.plusDays(1).toString().takeIf { deadline != it } }, { Text("Tomorrow") })
                AssistChip(
                    onClick = { pickDate = true },
                    label = { Text(deadline?.let { dueLabel(it) } ?: "Pick a date…") },
                    leadingIcon = { Icon(Icons.Default.Event, null, Modifier.size(18.dp)) },
                )
            }
            Row(Modifier.fillMaxWidth().padding(bottom = 12.dp), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onDismiss) { Text("Cancel") }
                Spacer(Modifier.width(8.dp))
                Button(onClick = ::add, enabled = title.isNotBlank()) { Text("Add") }
            }
        }
    }
    if (pickDate) DeadlinePicker(deadline, { pickDate = false }) { deadline = it; pickDate = false }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DeadlinePicker(current: String?, onDismiss: () -> Unit, onPick: (String) -> Unit) {
    val state = rememberDatePickerState(
        initialSelectedDateMillis = (parseDate(current) ?: LocalDate.now()).atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli(),
    )
    DatePickerDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = {
                state.selectedDateMillis?.let { onPick(Instant.ofEpochMilli(it).atZone(ZoneOffset.UTC).toLocalDate().toString()) } ?: onDismiss()
            }) { Text("OK") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    ) { DatePicker(state) }
}

// ── A room's todos ──

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RoomTodosScreen(model: AppModel, roomId: Int, onOpen: (Todo) -> Unit) {
    val room = model.room(roomId) ?: return
    val view = View.RoomView(roomId)
    val list = rememberLazyListState()
    var adding by remember { mutableStateOf(false) }
    Scaffold(
        topBar = {
            TopAppBar(
                navigationIcon = { IconButton(onClick = { model.pop() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") } },
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Avatar(room.name, 34.dp)
                        Spacer(Modifier.width(10.dp))
                        Column {
                            Text("Todos")
                            Text(room.name, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                },
            )
        },
        floatingActionButton = { NewTodoFab(list) { adding = true } },
    ) { padding ->
        TodoList(model, view, padding, onOpen, list)
    }
    if (adding) AddTodoSheet(model, view) { adding = false }
}

// ── One todo ──

/** Everything about one todo, editable, with its actions and files. */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun TodoSheet(model: AppModel, t: Todo, onDismiss: () -> Unit) {
    var title by remember { mutableStateOf(t.title) }
    var description by remember { mutableStateOf(t.description ?: "") }
    var priority by remember { mutableStateOf(t.priority) }
    var deadline by remember { mutableStateOf(t.deadline) }
    var categoryId by remember { mutableStateOf(t.categoryId) }
    var roomId by remember { mutableStateOf(t.roomId) }
    var pickDate by remember { mutableStateOf(false) }
    var pickCat by remember { mutableStateOf(false) }
    var pickRoom by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }
    val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    fun save() {
        val json = JSONObject()
        if (title.isNotBlank() && title.trim() != t.title) json.put("title", title.trim())
        val desc = description.trimEnd().ifBlank { null }
        if (desc != t.description) json.put("description", desc ?: JSONObject.NULL)
        if (priority != t.priority) json.put("priority", priority ?: JSONObject.NULL)
        if (deadline != t.deadline) json.put("deadline", deadline ?: JSONObject.NULL)
        if (categoryId != t.categoryId) json.put("categoryId", categoryId ?: JSONObject.NULL)
        if (roomId != t.roomId) json.put("roomId", roomId ?: JSONObject.NULL)
        if (json.length() > 0) model.saveTodo(t.id, json)
        onDismiss()
    }

    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = sheet) {
        Column(
            // The sheet opens full height: keep its top clear of the status bar.
            Modifier.fillMaxWidth().statusBarsPadding().verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp).imePadding().navigationBarsPadding(),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Surface(shape = RoundedCornerShape(8.dp), color = MaterialTheme.colorScheme.secondaryContainer) {
                    Text("#${t.id}", style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp))
                }
                Spacer(Modifier.width(10.dp))
                if (t.done) Text("Done", style = MaterialTheme.typography.labelLarge, color = LocalExtra.current.done)
                Spacer(Modifier.weight(1f))
                Text("added ${ago(t.createdAt)}", style = MaterialTheme.typography.labelMedium, color = LocalExtra.current.muted)
            }
            OutlinedTextField(title, { title = it }, label = { Text("Title") }, shape = RoundedCornerShape(14.dp), modifier = Modifier.fillMaxWidth())
            OutlinedTextField(
                description, { description = it },
                label = { Text("Description") },
                minLines = 3,
                shape = RoundedCornerShape(14.dp),
                keyboardOptions = KeyboardOptions.Default,
                modifier = Modifier.fillMaxWidth(),
            )

            TodoFiles(model, t)

            Section("Priority")
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                PRIORITY_NAMES.forEachIndexed { i, (value, label) ->
                    SegmentedButton(
                        selected = priority == value,
                        onClick = { priority = value },
                        shape = SegmentedButtonDefaults.itemShape(i, PRIORITY_NAMES.size),
                        icon = { if (value != null) Box(Modifier.requiredSize(9.dp).clip(CircleShape).background(priorityColor(value))) },
                    ) { Text(label) }
                }
            }

            Section("Deadline")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                val today = LocalDate.now()
                FilterChip(deadline == today.toString(), { deadline = today.toString() }, { Text("Today") })
                FilterChip(deadline == today.plusDays(1).toString(), { deadline = today.plusDays(1).toString() }, { Text("Tomorrow") })
                FilterChip(deadline == today.plusDays(7).toString(), { deadline = today.plusDays(7).toString() }, { Text("In a week") })
                AssistChip(
                    onClick = { pickDate = true },
                    label = { Text(deadline?.let { "${dueLabel(it)} · $it" } ?: "Pick a date…") },
                    leadingIcon = { Icon(Icons.Default.Event, null, Modifier.size(18.dp)) },
                )
                if (deadline != null) AssistChip(
                    onClick = { deadline = null },
                    label = { Text("No deadline") },
                    leadingIcon = { Icon(Icons.Default.Close, null, Modifier.size(18.dp)) },
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { pickCat = true }, modifier = Modifier.weight(1f)) {
                    Icon(Icons.Default.Folder, null, Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(categoryId?.let { model.pathName(it) } ?: "Inbox", maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                Column(Modifier.weight(1f)) {
                    OutlinedButton(onClick = { pickRoom = true }, modifier = Modifier.fillMaxWidth()) {
                        Icon(Icons.Default.Forum, null, Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(roomId?.let { model.room(it)?.name ?: "room $it" } ?: "No room", maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    DropdownMenu(pickRoom, { pickRoom = false }) {
                        DropdownMenuItem({ Text("No room") }, { roomId = null; pickRoom = false })
                        for (r in model.rooms) DropdownMenuItem({ Text(r.name) }, { roomId = r.id; pickRoom = false })
                    }
                }
            }

            ClaimRow(model, t, onDone = onDismiss)

            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { confirmDelete = true }) {
                    Icon(Icons.Default.Delete, "Delete", tint = MaterialTheme.colorScheme.error)
                }
                if (t.done) IconButton(onClick = { model.pruneTodo(t.id); onDismiss() }) {
                    Icon(Icons.Default.ContentCut, "Prune")
                }
                Spacer(Modifier.weight(1f))
                TextButton(onClick = onDismiss) { Text("Cancel") }
                Button(onClick = ::save, enabled = title.isNotBlank()) { Text("Save") }
            }
            Spacer(Modifier.height(8.dp))
        }
    }

    if (pickDate) DeadlinePicker(deadline, { pickDate = false }) { deadline = it; pickDate = false }
    if (pickCat) CategoryPicker(model, "Move to", "Inbox", categoryId, onDismiss = { pickCat = false }) {
        categoryId = it; pickCat = false
    }
    if (confirmDelete) Confirm("Delete #${t.id}?", t.title, "Delete", { confirmDelete = false }) {
        confirmDelete = false
        model.deleteTodo(t.id)
        onDismiss()
    }
}

@Composable
private fun Section(label: String) {
    Text(label, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
}

/** A todo's files, as in chat: images as a grid (tap for the viewer), PDFs as cards; add or remove them. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TodoFiles(model: AppModel, t: Todo) {
    val images = t.attachments.filter { it.isMedia }
    val files = t.attachments.filter { !it.isMedia }
    val pickImages = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia(AppModel.MAX_FILES)) { model.attachToTodo(t, it) }
    val pickPdf = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { model.attachToTodo(t, it) }
    var remove by remember { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Section("Files")
            Spacer(Modifier.weight(1f))
            if (model.todoUploading == t.id) CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
            if (t.attachments.isNotEmpty()) TextButton(onClick = { remove = !remove }) { Text(if (remove) "Done" else "Edit") }
            IconButton(onClick = { pickImages.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)) }) {
                Icon(Icons.Default.PhotoLibrary, "Attach photos or videos")
            }
            IconButton(onClick = { pickPdf.launch(arrayOf("application/pdf")) }) { Icon(Icons.Default.PictureAsPdf, "Attach a PDF") }
        }
        if (t.attachments.isEmpty()) {
            Text("No files. Attach photos, videos or PDFs to keep them with the todo.", style = MaterialTheme.typography.bodySmall, color = LocalExtra.current.muted)
        }
        if (images.isNotEmpty()) FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            val gallery = images.map { GalleryItem(it) }
            images.forEachIndexed { i, a ->
                Box(Modifier.size(96.dp)) {
                    MediaThumb(
                        Pic(model.fileUrl(a.id), a.width, a.height, a.name, a.isVideo, a.duration),
                        Modifier.fillMaxSize().clip(RoundedCornerShape(12.dp)).clickable { model.showImages(gallery, i) },
                        small = true,
                    )
                    if (remove) RemoveBadge(Modifier.align(Alignment.TopEnd)) { model.removeTodoAttachment(t, a) }
                }
            }
        }
        for (a in files) {
            Box {
                FileCard(
                    a.name, "PDF · ${sizeLabel(a.size)}",
                    busy = model.fetching == a.id,
                    container = MaterialTheme.colorScheme.surfaceContainerHigh,
                    content = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.fillMaxWidth(),
                    onClick = { model.openFile(a) },
                    onLongClick = { model.shareFile(a) },
                )
                if (remove) RemoveBadge(Modifier.align(Alignment.CenterEnd).padding(end = 8.dp)) { model.removeTodoAttachment(t, a) }
            }
        }
    }
}

@Composable
private fun RemoveBadge(modifier: Modifier, onClick: () -> Unit) {
    Surface(onClick = onClick, shape = CircleShape, color = MaterialTheme.colorScheme.error, contentColor = MaterialTheme.colorScheme.onError, modifier = modifier.padding(4.dp).size(26.dp)) {
        Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.Close, "Remove", Modifier.size(16.dp)) }
    }
}

/** Who's working on it, and the button to claim, release or take it over. */
@Composable
private fun ClaimRow(model: AppModel, t: Todo, onDone: () -> Unit) {
    val mine = t.claimedBy == model.agent
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surfaceContainerHigh, RoundedCornerShape(14.dp))
            .padding(start = 12.dp, end = 4.dp, top = 2.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (t.claimedBy != null) Avatar(t.claimedBy, 28.dp) else Icon(Icons.Default.Flag, null, Modifier.size(20.dp), tint = LocalExtra.current.accent)
        Spacer(Modifier.width(10.dp))
        CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
            Text(
                when {
                    t.claimedBy == null -> "Nobody's on it"
                    mine -> "You're on it"
                    else -> "${t.claimedBy} is on it"
                },
                Modifier.weight(1f),
            )
        }
        when {
            t.claimedBy == null -> TextButton(onClick = { model.claim(t); onDone() }) { Text("Claim") }
            mine -> TextButton(onClick = { model.release(t); onDone() }) { Text("Release") }
            else -> {
                TextButton(onClick = { model.release(t, force = true); onDone() }) { Text("Release") }
                TextButton(onClick = { model.claim(t, force = true); onDone() }) { Text("Take over") }
            }
        }
    }
}
