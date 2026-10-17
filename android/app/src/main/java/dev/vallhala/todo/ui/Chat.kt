package dev.vallhala.todo.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.Checklist
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.ErrorOutline
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PhotoCamera
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.TaskAlt
import androidx.compose.material.icons.filled.WifiOff
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathOperation
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.min
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Attachment
import dev.vallhala.todo.Draft
import dev.vallhala.todo.GalleryItem
import dev.vallhala.todo.Media
import dev.vallhala.todo.Message
import dev.vallhala.todo.Outgoing
import dev.vallhala.todo.Room
import dev.vallhala.todo.Screen
import dev.vallhala.todo.sizeLabel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import java.time.LocalDate
import kotlin.math.max

// ── What the log shows, newest first (the list is laid out bottom up) ──

private sealed interface Line { val key: Any }
private data class DayLine(val day: LocalDate) : Line { override val key get() = "d${day.toEpochDay()}" }
private data class UnreadLine(val count: Int) : Line { override val key get() = "unread" }
private data class EventLine(val m: Message) : Line { override val key get() = m.seq }
private data class MsgLine(val m: Message, val first: Boolean) : Line { override val key get() = m.seq }
private data class SendLine(val o: Outgoing, val first: Boolean) : Line { override val key get() = "o${o.key}" }

/** The log as lines: day separators, the unread mark, events, and messages grouped by author (oldest first). */
private fun lines(log: List<Message>, sending: List<Outgoing>, me: String, unreadFrom: Long?): List<Line> {
    val out = ArrayList<Line>()
    var day: LocalDate? = null
    var prevAuthor: String? = null
    val firstUnread = unreadFrom?.takeIf { it > 0 }?.let { from -> log.indexOfFirst { it.seq > from && it.author != me } }?.takeIf { it >= 0 }
    for ((i, m) in log.withIndex()) {
        val d = localDate(m.createdAt)
        if (d != day) { out += DayLine(d); day = d; prevAuthor = null }
        if (i == firstUnread) {
            out += UnreadLine(log.drop(i).count { it.author != me })
            prevAuthor = null
        }
        if (m.isEvent) {
            out += EventLine(m)
            prevAuthor = null
        } else {
            out += MsgLine(m, first = m.author != prevAuthor)
            prevAuthor = m.author
        }
    }
    for (o in sending) {
        val d = localDate(o.createdAt)
        if (d != day) { out += DayLine(d); day = d; prevAuthor = null }
        out += SendLine(o, first = prevAuthor != me)
        prevAuthor = me
    }
    return out
}

/** Every image in the log, oldest first, for the viewer to swipe through. */
private fun galleryOf(log: List<Message>) = log.flatMap { m ->
    m.attachments.filter { it.isImage }.map { GalleryItem(it, m.author, m.createdAt) }
}

// ── The screen ──

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatScreen(model: AppModel, room: Room, open: (AppDialog) -> Unit, onOpenTodo: (Int) -> Unit) {
    val extra = LocalExtra.current
    var info by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().background(extra.chatBg)) {
        ChatTopBar(model, room, onInfo = { info = true }, open = open)
        AnimatedVisibility(!model.linkUp, enter = expandVertically() + fadeIn(), exit = shrinkVertically() + fadeOut()) {
            Row(
                Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.tertiaryContainer).padding(horizontal = 16.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center,
            ) {
                CircularProgressIndicator(Modifier.size(14.dp), strokeWidth = 2.dp, color = MaterialTheme.colorScheme.onTertiaryContainer)
                Spacer(Modifier.width(8.dp))
                Text("Connecting…", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onTertiaryContainer)
            }
        }
        Box(Modifier.weight(1f)) {
            MessageList(model, room, onOpenTodo)
        }
        Composer(model, room)
    }
    if (info) RoomInfoSheet(model, room, onDismiss = { info = false }, open = open)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ChatTopBar(model: AppModel, room: Room, onInfo: () -> Unit, open: (AppDialog) -> Unit) {
    val openTodos = model.openIn(dev.vallhala.todo.View.RoomView(room.id))
    TopAppBar(
        navigationIcon = { IconButton(onClick = { model.pop() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") } },
        title = {
            Row(
                Modifier.clip(RoundedCornerShape(12.dp)).clickable(onClick = onInfo).padding(vertical = 4.dp, horizontal = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Avatar(room.name, 38.dp)
                Spacer(Modifier.width(10.dp))
                Column {
                    Text(room.name, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(
                        if (!model.linkUp) "connecting…" else subtitle(model, room),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        },
        actions = {
            IconButton(onClick = { model.push(Screen.RoomTodos(room.id)) }) {
                BadgedBox(badge = { if (openTodos > 0) Badge { Text("$openTodos") } }) {
                    Icon(Icons.Default.Checklist, "Room todos")
                }
            }
            var menu by remember { mutableStateOf(false) }
            IconButton(onClick = { menu = true }) { Icon(Icons.Default.MoreVert, "More") }
            DropdownMenu(menu, { menu = false }) {
                DropdownMenuItem({ Text("Room info") }, { menu = false; onInfo() })
                DropdownMenuItem({ Text("Edit room") }, { menu = false; open(AppDialog.EditRoom(room.id)) })
                DropdownMenuItem({ Text(if (room.id in model.pinned) "Unpin" else "Pin to top") }, { menu = false; model.togglePin(room.id) })
                DropdownMenuItem({ Text("Pruned messages" + if (room.prunedMessages > 0) " (${room.prunedMessages})" else "") }, {
                    menu = false; model.push(Screen.RoomPruned(room.id))
                })
                DropdownMenuItem({ Text("Prune messages") }, { menu = false; open(AppDialog.PruneMessages(room.id)) })
                HorizontalDivider()
                DropdownMenuItem({ Text("Delete room", color = MaterialTheme.colorScheme.error) }, { menu = false; open(AppDialog.DeleteRoom(room.id)) })
            }
        },
        colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
    )
}

private const val ACTIVE_SECONDS = 600

/** "you, claude-1, sam · 2 online", or the topic. */
private fun subtitle(model: AppModel, room: Room): String {
    val now = System.currentTimeMillis() / 1000
    val active = room.members.count { now - it.lastSeen < ACTIVE_SECONDS && it.name != model.agent }
    val names = room.members.sortedByDescending { it.lastSeen }.map { if (it.name == model.agent) "you" else it.name }
    return when {
        room.topic.isNotBlank() && names.isEmpty() -> room.topic
        names.isEmpty() -> "No members yet"
        else -> names.take(4).joinToString(", ") + (if (names.size > 4) " +${names.size - 4}" else "") +
            if (active > 0) " · $active online" else ""
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RoomInfoSheet(model: AppModel, room: Room, onDismiss: () -> Unit, open: (AppDialog) -> Unit) {
    val now = System.currentTimeMillis() / 1000
    ModalBottomSheet(onDismissRequest = onDismiss) {
        LazyColumn(contentPadding = PaddingValues(bottom = 24.dp)) {
            item {
                Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Avatar(room.name, 80.dp)
                    Spacer(Modifier.size(12.dp))
                    Text(room.name, style = MaterialTheme.typography.headlineSmall)
                    if (room.topic.isNotBlank()) {
                        Spacer(Modifier.size(6.dp))
                        Text(room.topic, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    Row(Modifier.padding(vertical = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        TextButton(onClick = { onDismiss(); open(AppDialog.EditRoom(room.id)) }) { Text("Edit") }
                        TextButton(onClick = { onDismiss(); model.push(Screen.RoomTodos(room.id)) }) { Text("Todos") }
                        TextButton(onClick = { onDismiss(); model.push(Screen.RoomPruned(room.id)) }) { Text("Pruned") }
                    }
                }
                HorizontalDivider()
                Text(
                    "${room.members.size} members",
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.padding(start = 24.dp, top = 16.dp, bottom = 4.dp),
                )
            }
            items(room.members.sortedByDescending { it.lastSeen }, key = { it.name }) { m ->
                Row(Modifier.fillMaxWidth().padding(horizontal = 24.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Box {
                        Avatar(m.name, 40.dp)
                        if (now - m.lastSeen < ACTIVE_SECONDS) Box(
                            Modifier.align(Alignment.BottomEnd).size(12.dp).clip(CircleShape)
                                .background(MaterialTheme.colorScheme.surfaceContainerLow).padding(2.dp).clip(CircleShape)
                                .background(LocalExtra.current.done),
                        )
                    }
                    Spacer(Modifier.width(14.dp))
                    Column {
                        Text(if (m.name == model.agent) "${m.name} (you)" else m.name, style = MaterialTheme.typography.bodyLarge)
                        Text(
                            if (now - m.lastSeen < ACTIVE_SECONDS) "active now" else "seen ${ago(m.lastSeen)}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}

// ── The log ──

@Composable
private fun MessageList(model: AppModel, room: Room, onOpenTodo: (Int) -> Unit) {
    val extra = LocalExtra.current
    val state = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val sending = model.outgoing.filter { it.roomId == room.id }
    val all = remember(model.log.toList(), sending.toList(), model.unreadFrom) {
        lines(model.log, sending, model.agent, model.unreadFrom).asReversed()
    }
    val images = remember(model.log.toList()) { galleryOf(model.log) }
    val clock = rememberClock()

    // Follow the newest message unless the user has scrolled up to read.
    var follow by remember { mutableStateOf(true) }
    var fresh by remember { mutableIntStateOf(0) }
    val atBottom by remember { derivedStateOf { state.firstVisibleItemIndex == 0 && state.firstVisibleItemScrollOffset < 120 } }
    LaunchedEffect(state) {
        snapshotFlow { atBottom }.collect { bottom ->
            if (bottom) { follow = true; fresh = 0 } else if (state.isScrollInProgress) follow = false
        }
    }
    val newest = all.firstOrNull()?.key
    var lastNewest by remember { mutableStateOf<Any?>(null) }
    var seenSeq by remember { mutableStateOf(model.log.lastOrNull()?.seq ?: 0L) }
    LaunchedEffect(newest) {
        if (newest == null) return@LaunchedEffect
        val first = lastNewest == null
        lastNewest = newest
        val top = model.log.lastOrNull()?.seq ?: 0L
        val arrived = model.log.count { it.seq > seenSeq && it.author != model.agent }
        seenSeq = top
        val mineNow = all.firstOrNull().let { it is SendLine || (it is MsgLine && it.m.author == model.agent) }
        when {
            first -> {
                // Opening: start at the first unread message if it's off screen, else at the bottom.
                val unread = all.indexOfFirst { it is UnreadLine }
                state.scrollToItem(0)
                if (unread > 0) {
                    snapshotFlow { state.layoutInfo.visibleItemsInfo.size }.first { it > 0 }
                    val last = state.layoutInfo.visibleItemsInfo.last().index
                    if (unread > last) {
                        state.scrollToItem(unread)
                        state.scrollBy(-(state.layoutInfo.viewportSize.height * 0.8f))
                        follow = false
                    }
                }
            }
            follow || mineNow -> { state.animateScrollToItem(0); fresh = 0 }
            else -> fresh += arrived
        }
    }

    // Near the top (the end of the reversed list): ask for older messages.
    LaunchedEffect(state) {
        snapshotFlow { (state.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0) to state.layoutInfo.totalItemsCount }
            .distinctUntilChanged()
            .collect { (last, total) -> if (total > 0 && last >= total - 4) model.loadOlder() }
    }

    if (all.isEmpty() && !model.logLoaded) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
        return
    }
    if (all.isEmpty()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Surface(shape = RoundedCornerShape(16.dp), color = extra.other, shadowElevation = 1.dp) {
                Column(Modifier.padding(horizontal = 24.dp, vertical = 18.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("No messages yet", style = MaterialTheme.typography.titleSmall)
                    Text("Say hello, or @mention someone.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
        return
    }

    BoxWithConstraints(Modifier.fillMaxSize()) {
        val maxBubble = min(maxWidth * 0.82f, 480.dp)
        LazyColumn(
            Modifier.fillMaxSize(),
            state = state,
            reverseLayout = true,
            contentPadding = PaddingValues(start = 8.dp, end = 8.dp, top = 8.dp, bottom = 6.dp),
        ) {
            items(all, key = { it.key }, contentType = { it::class }) { line ->
                when (line) {
                    is DayLine -> Pill(dayLabel(line.day), Modifier.padding(vertical = 8.dp))
                    is UnreadLine -> Box(
                        Modifier.fillMaxWidth().padding(vertical = 8.dp).background(MaterialTheme.colorScheme.surfaceContainerHighest.copy(alpha = 0.7f)),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(
                            if (line.count == 1) "1 unread message" else "${line.count} unread messages",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.padding(vertical = 6.dp),
                        )
                    }
                    is EventLine -> EventPill(model, line.m, clock, onOpenTodo)
                    is MsgLine -> MessageBubble(model, line.m, line.first, maxBubble, clock, images, onOpenTodo)
                    is SendLine -> SendingBubble(model, line.o, line.first, maxBubble, clock)
                }
            }
            item(key = "top") {
                Box(Modifier.fillMaxWidth().padding(12.dp), contentAlignment = Alignment.Center) {
                    when {
                        model.loadingOlder -> CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 2.dp)
                        model.hasOlder -> {}
                        model.log.size >= 1000 -> Pill("Older messages can't be loaded here")
                        else -> Pill(if (room.topic.isNotBlank()) room.topic else "This is the start of ${room.name}")
                    }
                }
            }
        }

        // "↓ N new" when messages arrive while reading further up; a plain ↓ when just scrolled up.
        AnimatedVisibility(
            !atBottom,
            Modifier.align(Alignment.BottomEnd).padding(12.dp),
            enter = scaleIn() + fadeIn(),
            exit = scaleOut() + fadeOut(),
        ) {
            Surface(
                onClick = { scope.launch { state.animateScrollToItem(0) }; fresh = 0 },
                shape = RoundedCornerShape(50),
                color = if (fresh > 0) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHigh,
                contentColor = if (fresh > 0) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface,
                shadowElevation = 3.dp,
            ) {
                Row(Modifier.padding(horizontal = if (fresh > 0) 14.dp else 8.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.KeyboardArrowDown, "Scroll to the newest", Modifier.size(22.dp))
                    if (fresh > 0) {
                        Spacer(Modifier.width(4.dp))
                        Text("$fresh new", style = MaterialTheme.typography.labelLarge)
                    }
                }
            }
        }
    }
}

@Composable
private fun Pill(text: String, modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Surface(
            shape = RoundedCornerShape(8.dp),
            color = MaterialTheme.colorScheme.surfaceContainerHighest.copy(alpha = 0.92f),
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
        ) {
            Text(text, style = MaterialTheme.typography.labelMedium, modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp))
        }
    }
}

/** A todo claimed, released, done...: a small centred line, tap to open the todo. */
@Composable
private fun EventPill(model: AppModel, m: Message, clock: (Long) -> String, onOpenTodo: (Int) -> Unit) {
    val todo = m.todoId?.takeIf { id -> model.todos.any { it.id == id } }
    Box(Modifier.fillMaxWidth().padding(vertical = 4.dp, horizontal = 24.dp), contentAlignment = Alignment.Center) {
        Surface(
            shape = RoundedCornerShape(50),
            color = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.85f),
            contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
            modifier = Modifier.clip(RoundedCornerShape(50)).then(if (todo != null) Modifier.clickable { onOpenTodo(todo) } else Modifier),
        ) {
            Row(Modifier.padding(horizontal = 12.dp, vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.TaskAlt, null, Modifier.size(14.dp))
                Spacer(Modifier.width(6.dp))
                Text(
                    buildEvent(m),
                    style = MaterialTheme.typography.labelMedium,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                )
                Spacer(Modifier.width(6.dp))
                Text(clock(m.createdAt), style = MaterialTheme.typography.labelSmall, color = faded(0.6f))
            }
        }
    }
}

@Composable
private fun faded(alpha: Float) = androidx.compose.material3.LocalContentColor.current.copy(alpha = alpha)

private fun buildEvent(m: Message) = androidx.compose.ui.text.buildAnnotatedString {
    if (m.author.isBlank()) {
        append(m.text.replaceFirstChar { it.uppercase() })
        return@buildAnnotatedString
    }
    pushStyle(androidx.compose.ui.text.SpanStyle(fontWeight = FontWeight.SemiBold))
    append(m.author)
    pop()
    append(" ")
    append(m.text)
}

// ── Bubbles ──

/** A rounded bubble whose first in a run has a little tail at the top, on the author's side. */
private class BubbleShape(private val mine: Boolean, private val tail: Boolean, private val radius: Dp, private val tailWidth: Dp) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline {
        val r = with(density) { radius.toPx() }
        val t = with(density) { tailWidth.toPx() }
        val body = Rect(if (mine) 0f else t, 0f, if (mine) size.width - t else size.width, size.height)
        val small = CornerRadius(if (tail) r * 0.2f else r, if (tail) r * 0.2f else r)
        val full = CornerRadius(r, r)
        val rect = Path().apply {
            addRoundRect(
                RoundRect(
                    body,
                    topLeft = if (!mine) small else full,
                    topRight = if (mine) small else full,
                    bottomRight = full,
                    bottomLeft = full,
                ),
            )
        }
        if (!tail) return Outline.Generic(rect)
        val tri = Path().apply {
            if (mine) {
                moveTo(size.width - t - r * 0.4f, 0f)
                lineTo(size.width - t * 0.15f, 0f)
                quadraticTo(size.width, 0f, size.width - t * 0.25f, t * 0.35f)
                lineTo(size.width - t, t * 1.5f)
            } else {
                moveTo(t + r * 0.4f, 0f)
                lineTo(t * 0.15f, 0f)
                quadraticTo(0f, 0f, t * 0.25f, t * 0.35f)
                lineTo(t, t * 1.5f)
            }
            close()
        }
        return Outline.Generic(Path.combine(PathOperation.Union, rect, tri))
    }
}

private val TAIL = 7.dp

/** The bubble around a message: its side, color, tail, and long-press. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun BubbleFrame(
    mine: Boolean,
    first: Boolean,
    maxWidth: Dp,
    onLongClick: () -> Unit,
    onClick: () -> Unit = {},
    menu: @Composable () -> Unit = {},
    content: @Composable () -> Unit,
) {
    val extra = LocalExtra.current
    val shape = remember(mine, first) { BubbleShape(mine, first, 14.dp, TAIL) }
    Row(
        Modifier.fillMaxWidth().padding(top = if (first) 6.dp else 1.5.dp),
        horizontalArrangement = if (mine) Arrangement.End else Arrangement.Start,
    ) {
        Box {
            Box(
                Modifier.widthIn(max = maxWidth)
                    .then(if (extra.dark) Modifier else Modifier.shadow(0.6.dp, shape, clip = false))
                    .clip(shape)
                    .background(if (mine) extra.mine else extra.other)
                    .combinedClickable(onClick = onClick, onLongClick = onLongClick)
                    .padding(start = if (mine) 0.dp else TAIL, end = if (mine) TAIL else 0.dp),
            ) {
                androidx.compose.runtime.CompositionLocalProvider(
                    androidx.compose.material3.LocalContentColor provides if (mine) extra.onMine else extra.onOther,
                ) { content() }
            }
            menu()
        }
    }
}

/** Time (and for ours, whether it's sent) as shown in the corner of a bubble. */
@Composable
private fun Meta(time: String, status: Status?, color: Color, modifier: Modifier = Modifier) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        Text(time, style = MaterialTheme.typography.labelSmall, color = color)
        if (status != null) {
            Spacer(Modifier.width(3.dp))
            Icon(
                when (status) { Status.Sending -> Icons.Default.Schedule; Status.Sent -> Icons.Default.Done; Status.Failed -> Icons.Default.ErrorOutline },
                status.name,
                Modifier.size(14.dp),
                tint = if (status == Status.Failed) MaterialTheme.colorScheme.error else color,
            )
        }
    }
}

private enum class Status { Sending, Sent, Failed }

/** The time over the corner of an image, on a dark scrim so it reads on any picture. */
@Composable
private fun ImageMeta(time: String, status: Status?, modifier: Modifier) {
    Surface(modifier.padding(6.dp), shape = RoundedCornerShape(10.dp), color = Color.Black.copy(alpha = 0.45f)) {
        Meta(time, status, Color.White, Modifier.padding(horizontal = 6.dp, vertical = 2.dp))
    }
}

/**
 * Text with the time tucked in at the end of its last line when it fits
 * (else on a line of its own), as chat apps do.
 */
@Composable
private fun TextWithMeta(text: AnnotatedString, meta: @Composable () -> Unit) {
    val holder = remember { arrayOfNulls<TextLayoutResult>(1) }
    Layout(content = {
        Text(text, style = MaterialTheme.typography.bodyLarge, onTextLayout = { holder[0] = it })
        meta()
    }) { ms, c ->
        val tp = ms[0].measure(c.copy(minWidth = 0))
        val mp = ms[1].measure(Constraints())
        val gap = 8.dp.roundToPx()
        val tl = holder[0]
        val lastWidth = tl?.let { val l = it.lineCount - 1; (it.getLineRight(l) - it.getLineLeft(l)).toInt() } ?: tp.width
        val rtl = tl?.let { it.getParagraphDirection(it.layoutInput.text.length.coerceAtLeast(1) - 1) == androidx.compose.ui.text.style.ResolvedTextDirection.Rtl } == true
        val inline = !rtl && lastWidth + gap + mp.width <= c.maxWidth
        val w = (if (inline) max(tp.width, lastWidth + gap + mp.width) else max(tp.width, mp.width)).coerceIn(c.minWidth, c.maxWidth)
        val h = if (inline) tp.height + mp.height / 4 else tp.height + mp.height
        layout(w, h) {
            tp.place(0, 0)
            mp.place(w - mp.width, h - mp.height)
        }
    }
}

@Composable
private fun MessageBubble(
    model: AppModel,
    m: Message,
    first: Boolean,
    maxBubble: Dp,
    clock: (Long) -> String,
    gallery: List<GalleryItem>,
    onOpenTodo: (Int) -> Unit,
) {
    val extra = LocalExtra.current
    val ctx = LocalContext.current
    val haptic = LocalHapticFeedback.current
    val mine = m.author == model.agent
    var menu by remember { mutableStateOf(false) }
    val onBubble = if (mine) extra.onMine else extra.onOther
    val metaColor = onBubble.copy(alpha = 0.62f)
    val images = m.attachments.filter { it.isImage }
    val files = m.attachments.filter { !it.isImage }
    val todoIds = model.todos.mapTo(HashSet()) { it.id }
    val linkColor = if (mine) extra.onMine else MaterialTheme.colorScheme.primary
    val text = remember(m.text, model.agent, todoIds.size, linkColor) {
        richText(m.text, linkColor, linkColor, model.agent) { id ->
            if (id in todoIds) { onOpenTodo(id); true } else false
        }
    }
    val time = clock(m.createdAt)
    val status = if (mine) Status.Sent else null

    BubbleFrame(
        mine = mine,
        first = first,
        maxWidth = maxBubble,
        onLongClick = { haptic.performHapticFeedback(HapticFeedbackType.LongPress); menu = true },
        menu = {
            DropdownMenu(menu, { menu = false }) {
                if (m.text.isNotBlank()) DropdownMenuItem(
                    { Text("Copy") }, { menu = false; copy(ctx, m.text); model.notice("Copied") },
                    leadingIcon = { Icon(Icons.Default.ContentCopy, null) },
                )
                for (a in m.attachments) DropdownMenuItem(
                    { Text("Share ${a.name}", maxLines = 1, overflow = TextOverflow.Ellipsis) }, { menu = false; model.shareFile(a) },
                    leadingIcon = { Icon(Icons.AutoMirrored.Filled.Send, null) },
                )
                DropdownMenuItem(
                    { Text("Prune #${m.id}") }, { menu = false; model.pruneMessage(m) },
                    leadingIcon = { Icon(Icons.Default.ContentCut, null) },
                )
            }
        },
    ) {
        val imgWidth = min(maxBubble - TAIL - 8.dp, 280.dp)
        Column(if (images.isNotEmpty()) Modifier.width(imgWidth + 8.dp) else Modifier) {
            if (first && !mine) {
                Text(
                    m.author,
                    style = MaterialTheme.typography.labelLarge,
                    color = nameColor(m.author, extra.dark),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.padding(start = 10.dp, end = 10.dp, top = 6.dp),
                )
            }
            if (images.isNotEmpty()) {
                BubbleImages(
                    images.map { Pic(model.fileUrl(it.id), it.width, it.height, it.name) },
                    width = imgWidth,
                    corner = 11.dp,
                    onClick = { i ->
                        val at = gallery.indexOfFirst { it.attachment == images[i] && it.time == m.createdAt && it.author == m.author }
                        model.showImages(gallery, at.coerceAtLeast(0))
                    },
                    onLongClick = { haptic.performHapticFeedback(HapticFeedbackType.LongPress); menu = true },
                    overlay = {
                        if (m.text.isBlank() && files.isEmpty()) ImageMeta(time, status, Modifier.align(Alignment.BottomEnd))
                    },
                    modifierPadding = 4.dp,
                )
            }
            for (a in files) {
                FileCard(
                    a.name,
                    "${if (a.isPdf) "PDF" else a.type.substringAfter('/').uppercase()} · ${sizeLabel(a.size)}",
                    busy = model.fetching == a.id,
                    container = onBubble.copy(alpha = 0.07f),
                    content = onBubble,
                    modifier = Modifier.padding(4.dp),
                    onClick = { model.openFile(a) },
                    onLongClick = { model.shareFile(a) },
                )
            }
            if (m.text.isNotBlank()) {
                Box(Modifier.padding(start = 10.dp, end = 8.dp, top = if (first && !mine || images.isNotEmpty() || files.isNotEmpty()) 2.dp else 6.dp, bottom = 5.dp)) {
                    TextWithMeta(text) { Meta(time, status, metaColor) }
                }
            } else if (files.isNotEmpty()) {
                Meta(time, status, metaColor, Modifier.align(Alignment.End).padding(end = 8.dp, bottom = 5.dp))
            }
        }
    }
}

/** A message we're sending: its pictures from the phone, a progress bar while uploading, and retry if it failed. */
@Composable
private fun SendingBubble(model: AppModel, o: Outgoing, first: Boolean, maxBubble: Dp, clock: (Long) -> String) {
    val extra = LocalExtra.current
    var menu by remember { mutableStateOf(false) }
    val images = o.drafts.filter { it.isImage }
    val files = o.drafts.filter { !it.isImage }
    val failed = o.failed
    val status = if (failed != null) Status.Failed else Status.Sending
    val metaColor = extra.onMine.copy(alpha = 0.62f)
    val time = clock(o.createdAt)
    BubbleFrame(
        mine = true,
        first = first,
        maxWidth = maxBubble,
        onClick = { if (failed != null) menu = true },
        onLongClick = { menu = true },
        menu = {
            DropdownMenu(menu, { menu = false }) {
                if (failed != null) DropdownMenuItem({ Text("Try again") }, { menu = false; model.retry(o) }, leadingIcon = { Icon(Icons.Default.Refresh, null) })
                DropdownMenuItem({ Text(if (failed != null) "Delete" else "Cancel") }, { menu = false; model.discard(o) }, leadingIcon = { Icon(Icons.Default.Close, null) })
            }
        },
    ) {
        val imgWidth = min(maxBubble - TAIL - 8.dp, 280.dp)
        Column(if (images.isNotEmpty()) Modifier.width(imgWidth + 8.dp) else Modifier) {
            if (images.isNotEmpty()) {
                BubbleImages(
                    images.map { Pic(it.file, it.width, it.height, it.name) },
                    width = imgWidth,
                    corner = 11.dp,
                    onClick = { if (failed != null) menu = true },
                    overlay = {
                        if (failed == null && o.progress < 1f) Box(Modifier.align(Alignment.Center)) {
                            Surface(shape = CircleShape, color = Color.Black.copy(alpha = 0.45f)) {
                                CircularProgressIndicator(
                                    progress = { o.progress },
                                    modifier = Modifier.padding(8.dp).size(36.dp),
                                    color = Color.White,
                                    trackColor = Color.White.copy(alpha = 0.25f),
                                    strokeWidth = 3.dp,
                                )
                            }
                        }
                        if (o.text.isBlank() && files.isEmpty()) ImageMeta(time, status, Modifier.align(Alignment.BottomEnd))
                    },
                    modifierPadding = 4.dp,
                )
            }
            for (d in files) {
                FileCard(d.name, "PDF · ${sizeLabel(d.size)}", busy = false, container = extra.onMine.copy(alpha = 0.07f), content = extra.onMine, modifier = Modifier.padding(4.dp))
            }
            if (o.drafts.isNotEmpty() && failed == null) {
                LinearProgressIndicator(
                    progress = { o.progress },
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp).clip(RoundedCornerShape(50)),
                    color = extra.onMine,
                    trackColor = extra.onMine.copy(alpha = 0.15f),
                    drawStopIndicator = {},
                )
            }
            if (o.text.isNotBlank()) {
                Box(Modifier.padding(start = 10.dp, end = 8.dp, top = if (o.drafts.isEmpty()) 6.dp else 2.dp, bottom = 5.dp)) {
                    TextWithMeta(AnnotatedString(o.text)) { Meta(time, status, metaColor) }
                }
            } else if (files.isNotEmpty()) {
                Meta(time, status, metaColor, Modifier.align(Alignment.End).padding(end = 8.dp, bottom = 5.dp))
            }
            if (failed != null) {
                Row(Modifier.padding(start = 10.dp, end = 8.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.WifiOff, null, Modifier.size(14.dp), tint = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.width(4.dp))
                    Text("$failed · tap to retry", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.error)
                }
            }
        }
    }
}

/** Images inside a bubble sit in a little frame of the bubble's color. */
@Composable
private fun BubbleImages(
    pics: List<Pic>,
    width: Dp,
    corner: Dp,
    onClick: (Int) -> Unit,
    onLongClick: (() -> Unit)? = null,
    overlay: @Composable androidx.compose.foundation.layout.BoxScope.() -> Unit,
    modifierPadding: Dp,
) {
    Box(Modifier.padding(modifierPadding)) {
        ImageGrid(pics, width, corner, onClick, onLongClick, overlay)
    }
}

private fun copy(ctx: Context, text: String) {
    ctx.getSystemService(ClipboardManager::class.java)?.setPrimaryClip(ClipData.newPlainText("message", text))
}

// ── Composer ──

/** The "@..." word the cursor is at the end of: its start and what's typed after the @. */
private fun mentionWord(v: TextFieldValue): Pair<Int, String>? {
    if (!v.selection.collapsed) return null
    val before = v.text.substring(0, v.selection.start)
    val m = Regex("(^|\\s)@([^\\s@]*)$").find(before) ?: return null
    val prefix = m.groupValues[2]
    return before.length - prefix.length - 1 to prefix
}

@Composable
private fun Composer(model: AppModel, room: Room) {
    val extra = LocalExtra.current
    val ctx = LocalContext.current
    var value by rememberSaveable(room.id, stateSaver = TextFieldValue.Saver) { mutableStateOf(TextFieldValue("")) }
    var attach by remember { mutableStateOf(false) }
    val drafts = model.drafts[room.id].orEmpty()
    val word = mentionWord(value)
    val now = System.currentTimeMillis() / 1000
    // Members other than us, most recently seen first.
    val matches = if (word == null) emptyList() else room.members
        .filter { it.name != model.agent && it.name.startsWith(word.second, ignoreCase = true) }
        .sortedByDescending { it.lastSeen }

    val pickImages = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia(AppModel.MAX_FILES)) { uris ->
        model.addDrafts(room.id, uris)
    }
    val pickPdf = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        model.addDrafts(room.id, uris)
    }
    var cameraFile by rememberSaveable { mutableStateOf<String?>(null) }
    val takePhoto = rememberLauncherForActivityResult(ActivityResultContracts.TakePicture()) { ok ->
        val f = cameraFile?.let { java.io.File(it) }
        if (ok && f != null && f.length() > 0) model.addDrafts(room.id, listOf(Uri.fromFile(f)), fromCamera = true)
        f?.let { file -> if (!ok) file.delete() }
    }
    fun camera() {
        val (f, uri) = Media.cameraTarget(ctx)
        cameraFile = f.path
        try {
            takePhoto.launch(uri)
        } catch (e: android.content.ActivityNotFoundException) {
            model.notice("No camera app here")
        }
    }

    fun insert(name: String) {
        val (start, _) = word ?: return
        val end = value.selection.start
        val text = value.text.substring(0, start) + "@$name " + value.text.substring(end)
        value = TextFieldValue(text, TextRange(start + name.length + 2))
    }

    val canSend = value.text.isNotBlank() || drafts.isNotEmpty()
    fun send() {
        if (!canSend) return
        model.send(room.id, value.text, model.takeDrafts(room.id))
        value = TextFieldValue("")
        attach = false
    }

    BackHandler(enabled = attach) { attach = false }

    Column(Modifier.fillMaxWidth().windowInsetsPadding(WindowInsets.navigationBars.union(WindowInsets.ime))) {
        AnimatedVisibility(matches.isNotEmpty(), enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            Surface(
                Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                shadowElevation = 2.dp,
            ) {
                LazyColumn(Modifier.heightIn(max = 220.dp), contentPadding = PaddingValues(vertical = 4.dp)) {
                    items(matches, key = { it.name }) { m ->
                        Row(
                            Modifier.fillMaxWidth().clickable { insert(m.name) }.padding(horizontal = 14.dp, vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Avatar(m.name, 32.dp)
                            Spacer(Modifier.width(12.dp))
                            Text(m.name, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                            Text(
                                if (now - m.lastSeen < ACTIVE_SECONDS) "active" else ago(m.lastSeen),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
        }

        AnimatedVisibility(attach, enter = slideInVertically { it / 2 } + fadeIn(), exit = slideOutVertically { it / 2 } + fadeOut()) {
            Surface(
                Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                shadowElevation = 3.dp,
            ) {
                Row(Modifier.padding(vertical = 16.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
                    AttachOption(Icons.Default.PhotoLibrary, "Gallery", Color(0xFF8E5BD8)) {
                        attach = false
                        pickImages.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
                    }
                    AttachOption(Icons.Default.PhotoCamera, "Camera", Color(0xFFD9467A)) { attach = false; camera() }
                    AttachOption(Icons.Default.PictureAsPdf, "PDF", Color(0xFF5468D8)) { attach = false; pickPdf.launch(arrayOf("application/pdf")) }
                }
            }
        }

        AnimatedVisibility(drafts.isNotEmpty() || model.preparing > 0, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            DraftTray(model, room.id, drafts, onAdd = { attach = true })
        }

        Row(
            Modifier.fillMaxWidth().padding(start = 6.dp, end = 6.dp, top = 4.dp, bottom = 6.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            Surface(
                Modifier.weight(1f),
                shape = RoundedCornerShape(24.dp),
                color = if (extra.dark) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainerLowest,
                shadowElevation = if (extra.dark) 0.dp else 1.dp,
            ) {
                Row(verticalAlignment = Alignment.Bottom) {
                    BasicTextField(
                        value = value,
                        onValueChange = { value = it },
                        textStyle = MaterialTheme.typography.bodyLarge.copy(color = MaterialTheme.colorScheme.onSurface),
                        cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                        maxLines = 5,
                        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
                        modifier = Modifier.weight(1f).padding(start = 18.dp, top = 13.dp, bottom = 13.dp, end = 4.dp),
                        decorationBox = { inner ->
                            Box {
                                if (value.text.isEmpty()) Text(
                                    if (drafts.isNotEmpty()) "Add a caption…" else "Message",
                                    style = MaterialTheme.typography.bodyLarge,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    maxLines = 1,
                                )
                                inner()
                            }
                        },
                    )
                    IconButton(onClick = { attach = !attach }) {
                        Icon(Icons.Default.AttachFile, "Attach", tint = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    AnimatedVisibility(value.text.isEmpty() && drafts.isEmpty(), enter = fadeIn() + scaleIn(), exit = fadeOut() + scaleOut()) {
                        IconButton(onClick = ::camera) {
                            Icon(Icons.Default.PhotoCamera, "Take a photo", tint = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
            Spacer(Modifier.width(6.dp))
            val bg by animateColorAsState(if (canSend) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHighest, label = "send")
            val fg by animateColorAsState(if (canSend) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurfaceVariant, label = "sendIcon")
            Surface(
                onClick = ::send,
                enabled = canSend,
                shape = CircleShape,
                color = bg,
                contentColor = fg,
                shadowElevation = if (canSend) 2.dp else 0.dp,
                modifier = Modifier.size(50.dp),
            ) {
                Box(contentAlignment = Alignment.Center) { Icon(Icons.AutoMirrored.Filled.Send, "Send", Modifier.padding(start = 3.dp)) }
            }
        }
    }
}

@Composable
private fun AttachOption(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, color: Color, onClick: () -> Unit) {
    Column(
        Modifier.clip(RoundedCornerShape(12.dp)).clickable(onClick = onClick).padding(horizontal = 12.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.size(54.dp).clip(CircleShape).background(color), contentAlignment = Alignment.Center) {
            Icon(icon, null, tint = Color.White)
        }
        Spacer(Modifier.size(6.dp))
        Text(label, style = MaterialTheme.typography.labelMedium)
    }
}

/** The files picked for the next message, each with a remove button, and a tile to add more. */
@Composable
private fun DraftTray(model: AppModel, roomId: Int, drafts: List<Draft>, onAdd: () -> Unit) {
    LazyRow(
        Modifier.fillMaxWidth().padding(top = 6.dp),
        contentPadding = PaddingValues(horizontal = 10.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        items(drafts, key = { it.file.path }) { d ->
            Box(Modifier.size(84.dp)) {
                if (d.isImage) {
                    Thumb(d.file, Modifier.fillMaxSize().clip(RoundedCornerShape(14.dp)), d.name)
                } else {
                    Surface(Modifier.fillMaxSize(), shape = RoundedCornerShape(14.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
                        Column(Modifier.padding(8.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                            Icon(Icons.Default.PictureAsPdf, null, tint = Color(0xFFE5483A))
                            Text(d.name, style = MaterialTheme.typography.labelSmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                            Text(sizeLabel(d.size), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
                Surface(
                    onClick = { model.removeDraft(roomId, d) },
                    shape = CircleShape,
                    color = Color.Black.copy(alpha = 0.6f),
                    contentColor = Color.White,
                    modifier = Modifier.align(Alignment.TopEnd).padding(4.dp).size(24.dp),
                ) {
                    Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.Close, "Remove ${d.name}", Modifier.size(16.dp)) }
                }
            }
        }
        if (model.preparing > 0) item(key = "preparing") {
            Box(Modifier.size(84.dp).clip(RoundedCornerShape(14.dp)).background(MaterialTheme.colorScheme.surfaceContainerHigh), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(Modifier.size(26.dp), strokeWidth = 2.5.dp)
            }
        }
        if (drafts.size < AppModel.MAX_FILES) item(key = "add") {
            Surface(
                onClick = onAdd,
                shape = RoundedCornerShape(14.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                modifier = Modifier.size(84.dp),
            ) {
                Box(contentAlignment = Alignment.Center) { Icon(Icons.Default.Add, "Add more", tint = MaterialTheme.colorScheme.primary) }
            }
        }
    }
}

// ── Pruned messages ──

/** The room's pruned messages, most recently pruned first, to restore or delete for good. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PrunedMessagesScreen(model: AppModel, room: Room, open: (AppDialog) -> Unit) {
    LaunchedEffect(room.id, room.prunedMessages) { model.loadPrunedLog(room.id) }
    val list = model.prunedLog.orEmpty().filter { it.roomId == room.id }
        .sortedWith(compareByDescending<Message> { it.prunedAt ?: 0 }.thenByDescending { it.seq })
    val clock = rememberClock()
    androidx.compose.material3.Scaffold(
        topBar = {
            TopAppBar(
                navigationIcon = { IconButton(onClick = { model.pop() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") } },
                title = {
                    Column {
                        Text("Pruned messages")
                        Text(room.name, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                },
                actions = {
                    if (list.isNotEmpty()) TextButton(onClick = { open(AppDialog.DeleteAllPrunedMessages(room.id)) }) {
                        Text("Delete all", color = MaterialTheme.colorScheme.error)
                    }
                },
            )
        },
    ) { padding ->
        LazyColumn(Modifier.fillMaxSize().padding(padding), contentPadding = PaddingValues(vertical = 8.dp)) {
            if (list.isEmpty()) item {
                EmptyState(Icons.Default.ContentCut, "Nothing pruned", "Long-press a message to prune it and free its id.")
            }
            items(list, key = { it.seq }) { m ->
                Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp), verticalAlignment = Alignment.Top) {
                    Avatar(m.author, 36.dp)
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(m.author, style = MaterialTheme.typography.labelLarge, color = nameColor(m.author, LocalExtra.current.dark))
                            Spacer(Modifier.width(8.dp))
                            Text(
                                "p${m.id} · ${dayLabel(localDate(m.createdAt))} ${clock(m.createdAt)}",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        val body = m.text.ifBlank { attachmentSummary(m.attachments) }
                        Text(body, style = MaterialTheme.typography.bodyMedium, maxLines = 4, overflow = TextOverflow.Ellipsis)
                        m.prunedAt?.let {
                            Text("pruned ${ago(it)}", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    IconButton(onClick = { model.restoreMessage(m) }) { Icon(Icons.AutoMirrored.Filled.Undo, "Restore") }
                    IconButton(onClick = { model.deletePrunedMessage(m) }) {
                        Icon(Icons.Default.Delete, "Delete for good", tint = MaterialTheme.colorScheme.error)
                    }
                }
            }
        }
    }
}

/** "📷 Photo", "📷 3 photos", "📄 notes.pdf". */
fun attachmentSummary(list: List<Attachment>): String {
    val images = list.count { it.isImage }
    val files = list.filter { !it.isImage }
    return when {
        images == 1 && files.isEmpty() -> "📷 Photo"
        images > 1 && files.isEmpty() -> "📷 $images photos"
        images == 0 && files.size == 1 -> "📄 ${files[0].name}"
        images == 0 && files.isNotEmpty() -> "📄 ${files.size} files"
        list.isEmpty() -> ""
        else -> "📎 ${list.size} attachments"
    }
}
