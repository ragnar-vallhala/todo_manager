package dev.vallhala.todo.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AddComment
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Forum
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.PushPin
import androidx.compose.material.icons.filled.SearchOff
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Room
import dev.vallhala.todo.sizeLabel

/** The rooms, as a chat list: pinned first, then by latest message; filtered by [query]. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatList(model: AppModel, padding: PaddingValues, query: String, open: (AppDialog) -> Unit) {
    val q = query.trim()
    val list = model.sortedRooms().filter {
        q.isEmpty() || it.name.contains(q, ignoreCase = true) || it.topic.contains(q, ignoreCase = true) ||
            model.previews[it.id]?.last?.text?.contains(q, ignoreCase = true) == true
    }
    val clock = rememberClock()
    PullToRefreshBox(isRefreshing = model.loading, onRefresh = { model.refresh() }, modifier = Modifier.fillMaxSize().padding(padding)) {
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(top = 4.dp, bottom = 88.dp)) {
            if (list.isEmpty()) item {
                when {
                    q.isNotEmpty() -> EmptyState(Icons.Default.SearchOff, "No rooms match “$q”", "Search looks at names, topics and the latest message.")
                    !model.loaded && model.offline != null -> EmptyState(Icons.Default.Forum, "Can't reach the server", "Pull down to retry, or check Settings.")
                    !model.loaded -> {}
                    else -> EmptyState(Icons.Default.Forum, "No rooms yet", "Rooms are where you and your agents talk about the work.") {
                        FilledTonalButton(onClick = { open(AppDialog.NewRoom) }) { Text("New room") }
                    }
                }
            }
            items(list, key = { it.id }) { r ->
                RoomRow(model, r, clock, onClick = { model.openChat(r.id) }, open = open)
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun RoomRow(
    model: AppModel,
    r: Room,
    clock: (Long) -> String,
    onClick: () -> Unit,
    open: ((AppDialog) -> Unit)?,
) {
    val p = model.previews[r.id]
    val last = p?.last
    val unread = p?.unread ?: 0
    val pinned = r.id in model.pinned
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    val haptic = LocalHapticFeedback.current
    var menu by remember { mutableStateOf(false) }
    Box {
        Row(
            Modifier.fillMaxWidth()
                .combinedClickable(
                    onClick = onClick,
                    onLongClick = if (open == null) null else ({ haptic.performHapticFeedback(HapticFeedbackType.LongPress); menu = true }),
                )
                .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Avatar(r.name, 52.dp)
            Spacer(Modifier.width(14.dp))
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        r.name,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = if (unread > 0) FontWeight.Bold else FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f),
                    )
                    last?.let {
                        Text(
                            listTime(it.createdAt, clock),
                            style = MaterialTheme.typography.labelMedium,
                            color = if (unread > 0) MaterialTheme.colorScheme.primary else muted,
                            fontWeight = if (unread > 0) FontWeight.SemiBold else null,
                        )
                    }
                }
                Spacer(Modifier.height(3.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        preview(model, r),
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (unread > 0) MaterialTheme.colorScheme.onSurface else muted,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f),
                    )
                    if (p?.mentioned == true) {
                        Spacer(Modifier.width(6.dp))
                        Bubble("@")
                    }
                    if (unread > 0) {
                        Spacer(Modifier.width(6.dp))
                        Bubble(if (unread >= 100) "99+" else "$unread")
                    } else if (pinned) {
                        Spacer(Modifier.width(6.dp))
                        Icon(Icons.Default.PushPin, "Pinned", Modifier.size(16.dp), tint = muted)
                    }
                }
            }
        }
        if (open != null) DropdownMenu(menu, { menu = false }) {
            DropdownMenuItem({ Text(if (pinned) "Unpin" else "Pin to top") }, { menu = false; model.togglePin(r.id) })
            if (unread > 0) DropdownMenuItem({ Text("Mark as read") }, { menu = false; model.markRead(r) })
            DropdownMenuItem({ Text("Edit room") }, { menu = false; open(AppDialog.EditRoom(r.id)) })
            HorizontalDivider()
            DropdownMenuItem({ Text("Delete room", color = MaterialTheme.colorScheme.error) }, { menu = false; open(AppDialog.DeleteRoom(r.id)) })
        }
    }
}

@Composable
private fun Bubble(text: String) {
    Box(
        Modifier.defaultMinSize(minWidth = 22.dp, minHeight = 22.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primary)
            .padding(horizontal = 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = MaterialTheme.colorScheme.onPrimary, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold)
    }
}

/** "You: on it", "sam: 📷 Photo", "claude claimed #3 …", or the topic for a quiet room. */
@Composable
private fun preview(model: AppModel, r: Room) = buildAnnotatedString {
    val m = model.previews[r.id]?.last
    val muted = MaterialTheme.colorScheme.onSurfaceVariant
    when {
        m == null -> withStyle(SpanStyle(fontStyle = FontStyle.Italic, color = muted)) {
            append(r.topic.ifBlank { if (r.cursor == 0L) "No messages yet" else "…" })
        }
        m.isEvent -> withStyle(SpanStyle(fontStyle = FontStyle.Italic)) {
            append(if (m.author.isBlank()) m.text.replaceFirstChar { it.uppercase() } else "${m.author} ${m.text}")
        }
        else -> {
            val who = if (m.author == model.agent) "You" else m.author
            withStyle(SpanStyle(fontWeight = FontWeight.Medium)) { append("$who: ") }
            val files = attachmentSummary(m.attachments)
            val text = m.text.replace('\n', ' ')
            when {
                files.isEmpty() -> append(text)
                text.isBlank() -> append(files)
                else -> append("${files.substringBefore(' ')} $text")
            }
        }
    }
}

/** Where files shared from another app go: pick a room. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SharePicker(model: AppModel) {
    val clock = rememberClock()
    Scaffold(
        topBar = {
            TopAppBar(
                navigationIcon = { IconButton(onClick = { model.cancelShare() }) { Icon(Icons.Default.Close, "Cancel") } },
                title = {
                    Column {
                        Text("Send to…")
                        Text(
                            "${model.shared.size} ${if (model.shared.size == 1) "file" else "files"}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                },
            )
        },
    ) { padding ->
        LazyColumn(Modifier.fillMaxSize().padding(padding)) {
            item {
                LazyRow(
                    contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(model.shared, key = { it.file.path }) { d ->
                        if (d.isMedia) MediaThumb(Pic(d.file, d.width, d.height, d.name, d.isVideo, d.duration), Modifier.size(72.dp).clip(RoundedCornerShape(12.dp)), small = true)
                        else Surface(Modifier.size(72.dp), shape = RoundedCornerShape(12.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
                            Column(Modifier.padding(6.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                                Icon(Icons.Default.PictureAsPdf, null, tint = Color(0xFFE5483A))
                                Text(sizeLabel(d.size), style = MaterialTheme.typography.labelSmall)
                            }
                        }
                    }
                }
                HorizontalDivider()
            }
            if (model.rooms.isEmpty()) item {
                EmptyState(Icons.Default.AddComment, "No rooms to send to", "Make a room first, then share again.")
            }
            items(model.sortedRooms(), key = { it.id }) { r ->
                RoomRow(model, r, clock, onClick = { model.shareTo(r.id) }, open = null)
            }
        }
    }
}
