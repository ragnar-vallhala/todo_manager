package dev.vallhala.todo.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Notes
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Event
import androidx.compose.material.icons.filled.Flag
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.Forum
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DatePicker
import androidx.compose.material3.DatePickerDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberDatePickerState
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Todo
import dev.vallhala.todo.body
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset

/**
 * One todo: tick box, title, and a line of what's set (priority, deadline,
 * notes, room, claim). [showTag] adds its category, for views that don't group by it.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TodoRow(model: AppModel, t: Todo, showTag: Boolean, onOpen: () -> Unit) {
    val extra = LocalExtra.current
    val st = model.styleOf(t.categoryId)
    val bg = styleBg(st)
    val text = styleText(st)?.takeIf { bg != null } ?: MaterialTheme.colorScheme.onSurface
    val muted = if (bg != null) text.copy(alpha = 0.72f) else extra.muted
    Row(
        Modifier.fillMaxWidth()
            .then(if (bg != null) Modifier.background(bg) else Modifier)
            .clickable(onClick = onOpen)
            .padding(start = 4.dp, end = 12.dp, top = 2.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Checkbox(checked = t.done, onCheckedChange = { model.setDone(t, it) })
        Column(Modifier.weight(1f).padding(vertical = 8.dp)) {
            Text(
                t.title,
                color = if (t.done) muted else text,
                textDecoration = if (t.done) TextDecoration.LineThrough else null,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 3,
                overflow = TextOverflow.Ellipsis,
            )
            val tags = showTag && t.categoryId != null
            val hasMeta = t.priority != null || t.deadline != null || t.description != null || t.claimedBy != null ||
                tags || (t.roomId != null && model.view !is dev.vallhala.todo.View.RoomView)
            if (hasMeta) FlowRow(
                Modifier.padding(top = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.Center,
            ) {
                CompositionLocalProvider(LocalContentColor provides muted) {
                    Text("#${t.id}", style = MaterialTheme.typography.labelMedium, color = muted)
                    t.priority?.let {
                        Text(MARKS[it] ?: "", style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold,
                            color = if (bg != null) text else if (it == "high") extra.danger else extra.warn)
                    }
                    t.deadline?.let {
                        Meta(Icons.Default.Event, dueLabel(it), if (overdue(it) && !t.done) extra.danger else muted)
                    }
                    if (t.description != null) Icon(Icons.AutoMirrored.Filled.Notes, "Has notes", Modifier.size(15.dp), tint = muted)
                    if (tags) {
                        val cst = model.styleOf(t.categoryId)
                        Meta(Icons.Default.Folder, model.pathName(t.categoryId), if (bg == null) styleText(cst) ?: extra.category else muted)
                    }
                    if (t.roomId != null && model.view !is dev.vallhala.todo.View.RoomView) {
                        Meta(Icons.Default.Forum, model.room(t.roomId)?.name ?: "room ${t.roomId}", muted)
                    }
                    t.claimedBy?.let {
                        Meta(Icons.Default.Flag, if (it == model.agent) "you" else it, if (bg != null) text else extra.accent)
                    }
                }
            }
        }
    }
}

@Composable
private fun Meta(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, color: Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, null, Modifier.size(14.dp), tint = color)
        Spacer(Modifier.width(3.dp))
        Text(label, style = MaterialTheme.typography.labelMedium, color = color, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/** Everything about one todo, editable, with its actions. */
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
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp).imePadding().navigationBarsPadding(),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("#${t.id}", style = MaterialTheme.typography.titleMedium, color = LocalExtra.current.muted)
                Spacer(Modifier.weight(1f))
                Text("added ${ago(t.createdAt)}", style = MaterialTheme.typography.labelMedium, color = LocalExtra.current.muted)
            }
            OutlinedTextField(title, { title = it }, label = { Text("Title") }, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(
                description, { description = it },
                label = { Text("Description") },
                minLines = 3,
                keyboardOptions = KeyboardOptions.Default,
                modifier = Modifier.fillMaxWidth(),
            )

            Text("Priority", style = MaterialTheme.typography.labelLarge)
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                PRIORITY_NAMES.forEachIndexed { i, (value, label) ->
                    SegmentedButton(
                        selected = priority == value,
                        onClick = { priority = value },
                        shape = SegmentedButtonDefaults.itemShape(i, PRIORITY_NAMES.size),
                    ) { Text(label) }
                }
            }

            Text("Deadline", style = MaterialTheme.typography.labelLarge)
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
                TextButton(onClick = ::save, enabled = title.isNotBlank()) { Text("Save") }
            }
            Spacer(Modifier.height(8.dp))
        }
    }

    if (pickDate) {
        val state = rememberDatePickerState(
            initialSelectedDateMillis = (parseDate(deadline) ?: LocalDate.now()).atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli(),
        )
        DatePickerDialog(
            onDismissRequest = { pickDate = false },
            confirmButton = {
                TextButton(onClick = {
                    state.selectedDateMillis?.let { deadline = Instant.ofEpochMilli(it).atZone(ZoneOffset.UTC).toLocalDate().toString() }
                    pickDate = false
                }) { Text("OK") }
            },
            dismissButton = { TextButton(onClick = { pickDate = false }) { Text("Cancel") } },
        ) { DatePicker(state) }
    }
    if (pickCat) CategoryPicker(model, "Move to", "Inbox", categoryId, onDismiss = { pickCat = false }) {
        categoryId = it; pickCat = false
    }
    if (confirmDelete) Confirm("Delete #${t.id}?", t.title, "Delete", { confirmDelete = false }) {
        confirmDelete = false
        model.deleteTodo(t.id)
        onDismiss()
    }
}

/** Who's working on it, and the button to claim, release or take it over. */
@Composable
private fun ClaimRow(model: AppModel, t: Todo, onDone: () -> Unit) {
    val mine = t.claimedBy == model.agent
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surfaceContainerHigh, RoundedCornerShape(10.dp))
            .padding(start = 12.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Icons.Default.Flag, null, Modifier.size(18.dp), tint = LocalExtra.current.accent)
        Spacer(Modifier.width(8.dp))
        Text(
            when {
                t.claimedBy == null -> "Nobody's on it"
                mine -> "You're on it"
                else -> "${t.claimedBy} is on it"
            },
            Modifier.weight(1f),
        )
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
