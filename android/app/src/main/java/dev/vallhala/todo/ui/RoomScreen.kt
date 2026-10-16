package dev.vallhala.todo.ui

import androidx.compose.foundation.background
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Message
import dev.vallhala.todo.Room
import dev.vallhala.todo.Todo

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RoomScreen(model: AppModel, room: Room, padding: PaddingValues, onOpen: (Todo) -> Unit) {
    var tab by rememberSaveable(room.id) { mutableIntStateOf(0) }
    val open = model.openIn(dev.vallhala.todo.View.RoomView(room.id))
    Column(Modifier.fillMaxSize().padding(padding)) {
        RoomHeader(model, room)
        PrimaryTabRow(selectedTabIndex = tab) {
            Tab(tab == 0, { tab = 0 }, text = { Text("Log") })
            Tab(tab == 1, { tab = 1 }, text = { Text(if (open > 0) "Todos ($open)" else "Todos") })
        }
        if (tab == 0) {
            Log(model, Modifier.weight(1f))
            Composer(model, room)
        } else {
            TodoList(model, PaddingValues(), Modifier.weight(1f), onOpen)
        }
    }
}

/** The topic and members; folded to one line (tap to toggle), since it can take half the screen. */
@Composable
private fun RoomHeader(model: AppModel, room: Room) {
    val open = model.roomHeader
    val muted = LocalExtra.current.muted
    Column(Modifier.fillMaxWidth().clickable { model.toggleRoomHeader() }.padding(horizontal = 16.dp, vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                room.topic.ifBlank { "No topic" },
                Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
                color = muted,
                maxLines = if (open) Int.MAX_VALUE else 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (!open && room.members.isNotEmpty()) {
                val recent = room.members.count { System.currentTimeMillis() / 1000 - it.lastSeen < 600 }
                Text(
                    "  ${room.members.size} members" + if (recent > 0) " · $recent active" else "",
                    style = MaterialTheme.typography.labelMedium,
                    color = muted,
                )
            }
            Icon(
                if (open) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                if (open) "Hide topic and members" else "Show topic and members",
                tint = muted,
            )
        }
        if (open && room.members.isNotEmpty()) LazyRow(
            Modifier.padding(top = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            items(room.members.sortedByDescending { it.lastSeen }, key = { it.name }) { m ->
                Surface(shape = RoundedCornerShape(50), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
                    Row(Modifier.padding(horizontal = 10.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        val recent = System.currentTimeMillis() / 1000 - m.lastSeen < 600
                        Box(Modifier.size(7.dp).background(if (recent) LocalExtra.current.done else LocalExtra.current.muted, CircleShape))
                        Spacer(Modifier.width(6.dp))
                        Text(
                            buildAnnotatedString {
                                withStyle(SpanStyle(fontWeight = if (m.name == model.agent) FontWeight.Bold else null)) { append(m.name) }
                                withStyle(SpanStyle(color = LocalExtra.current.muted)) { append(" · ${ago(m.lastSeen)}") }
                            },
                            style = MaterialTheme.typography.labelMedium,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun Log(model: AppModel, modifier: Modifier) {
    val msgs = model.log
    val state = rememberLazyListState()
    // Newest at the bottom; keep following it as messages arrive.
    LaunchedEffect(msgs.size) { if (msgs.isNotEmpty()) state.scrollToItem(msgs.size - 1) }
    if (msgs.isEmpty()) {
        Box(modifier.fillMaxWidth().padding(24.dp), contentAlignment = Alignment.Center) {
            Text("No messages yet.", color = LocalExtra.current.muted)
        }
        return
    }
    LazyColumn(modifier.fillMaxWidth(), state = state, contentPadding = PaddingValues(vertical = 8.dp)) {
        items(msgs, key = { it.id }) { m -> MessageRow(model, m) }
    }
}

@Composable
private fun MessageRow(model: AppModel, m: Message) {
    val extra = LocalExtra.current
    val mine = m.author == model.agent
    val mentioned = !mine && Regex("@${Regex.escape(model.agent)}\\b").containsMatchIn(m.text)
    if (m.isEvent) {
        Text(
            buildAnnotatedString {
                withStyle(SpanStyle(fontWeight = FontWeight.SemiBold)) { append(m.author) }
                append(" ${m.text}")
                withStyle(SpanStyle(fontStyle = FontStyle.Normal)) { append("  · ${ago(m.createdAt)}") }
            },
            style = MaterialTheme.typography.bodySmall,
            fontStyle = FontStyle.Italic,
            color = extra.muted,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
        )
        return
    }
    Column(
        Modifier.fillMaxWidth()
            .then(if (mentioned) Modifier.background(MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f)) else Modifier)
            .padding(horizontal = 16.dp, vertical = 6.dp),
    ) {
        Row(verticalAlignment = Alignment.Bottom) {
            Text(m.author, fontWeight = FontWeight.SemiBold, color = if (mine) extra.accent else MaterialTheme.colorScheme.onSurface,
                style = MaterialTheme.typography.labelLarge)
            Spacer(Modifier.width(8.dp))
            Text(ago(m.createdAt), style = MaterialTheme.typography.labelSmall, color = extra.muted)
        }
        Text(highlightMentions(m.text, extra.accent), style = MaterialTheme.typography.bodyMedium)
    }
}

private fun highlightMentions(text: String, color: androidx.compose.ui.graphics.Color) = buildAnnotatedString {
    var last = 0
    for (match in Regex("(?<=^|\\s)@[^\\s@]+").findAll(text)) {
        append(text.substring(last, match.range.first))
        withStyle(SpanStyle(color = color, fontWeight = FontWeight.SemiBold)) { append(match.value) }
        last = match.range.last + 1
    }
    append(text.substring(last))
}

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
    var value by remember(room.id) { mutableStateOf(TextFieldValue("")) }
    val word = mentionWord(value)
    // Members other than us, most recently seen first.
    val matches = if (word == null) emptyList() else room.members
        .filter { it.name != model.agent && it.name.startsWith(word.second, ignoreCase = true) }
        .sortedByDescending { it.lastSeen }.map { it.name }

    fun insert(name: String) {
        val (start, _) = word ?: return
        val end = value.selection.start
        val text = value.text.substring(0, start) + "@$name " + value.text.substring(end)
        value = TextFieldValue(text, TextRange(start + name.length + 2))
    }

    fun send() {
        val text = value.text.trim()
        if (text.isEmpty()) return
        model.post(text) { value = TextFieldValue("") }
    }

    Column(Modifier.fillMaxWidth()) {
        if (matches.isNotEmpty()) {
            HorizontalDivider()
            LazyColumn(Modifier.heightIn(max = 180.dp).background(MaterialTheme.colorScheme.surfaceContainer)) {
                items(matches, key = { it }) { name ->
                    Text(
                        "@$name",
                        Modifier.fillMaxWidth().clickable { insert(name) }.padding(horizontal = 16.dp, vertical = 10.dp),
                    )
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value, { value = it },
                placeholder = { Text("Message ${room.name}…  @ to mention") },
                maxLines = 6,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
                modifier = Modifier.weight(1f),
            )
            IconButton(onClick = ::send, enabled = value.text.isNotBlank()) {
                Icon(Icons.AutoMirrored.Filled.Send, "Send", tint = if (value.text.isNotBlank()) MaterialTheme.colorScheme.primary else LocalExtra.current.muted)
            }
        }
    }
}
