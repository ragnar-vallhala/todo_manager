package dev.vallhala.todo.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Inbox
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.CatStyle
import dev.vallhala.todo.ServerTheme

@Composable
fun TextPrompt(
    title: String,
    initial: String = "",
    label: String? = null,
    confirm: String = "Save",
    singleLine: Boolean = true,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var value by remember { mutableStateOf(TextFieldValue(initial, TextRange(initial.length))) }
    val focus = remember { FocusRequester() }
    val ok = value.text.isNotBlank()
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = value,
                onValueChange = { value = it },
                label = label?.let { { Text(it) } },
                singleLine = singleLine,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = androidx.compose.foundation.text.KeyboardActions(onDone = { if (ok) onConfirm(value.text) }),
                modifier = Modifier.fillMaxWidth().focusRequester(focus),
            )
            LaunchedEffect(Unit) { focus.requestFocus() }
        },
        confirmButton = { TextButton(enabled = ok, onClick = { onConfirm(value.text) }) { Text(confirm) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun Confirm(title: String, text: String, confirm: String, onDismiss: () -> Unit, onConfirm: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(text) },
        confirmButton = {
            TextButton(onClick = onConfirm) { Text(confirm, color = MaterialTheme.colorScheme.error) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

/** Picks a category (or [rootLabel] for none). Categories in [exclude]'s subtree can't be picked. */
@Composable
fun CategoryPicker(
    model: AppModel,
    title: String,
    rootLabel: String,
    current: Int?,
    exclude: Int? = null,
    onDismiss: () -> Unit,
    onPick: (Int?) -> Unit,
) {
    // Live categories, plus the archive's when the current one is archived.
    val rows = remember(model.cats) {
        val live = model.flatten(null)
        if (current != null && model.inArchive(current)) {
            live + model.archiveRoots().flatMap { root -> listOf(root to 0) + model.flatten(root.id).map { it.first to it.second + 1 } }
        } else live
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            LazyColumn(Modifier.heightIn(max = 420.dp)) {
                item {
                    PickRow(selected = current == null, onClick = { onPick(null) }) {
                        Icon(Icons.Default.Inbox, null, Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(rootLabel)
                    }
                }
                items(rows, key = { it.first.id }) { (c, depth) ->
                    val blocked = exclude != null && model.isWithin(c.id, exclude)
                    val st = model.styleOf(c.id)
                    PickRow(selected = current == c.id, enabled = !blocked, onClick = { onPick(c.id) }, indent = depth) {
                        Text(
                            buildAnnotatedString { withStyle(styleSpan(st)) { append(c.name) } },
                            color = if (blocked) MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f) else Color.Unspecified,
                            modifier = styleBg(st)?.let { Modifier.background(it, RoundedCornerShape(4.dp)).padding(horizontal = 6.dp) } ?: Modifier,
                        )
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
private fun PickRow(
    selected: Boolean,
    enabled: Boolean = true,
    indent: Int = 0,
    onClick: () -> Unit,
    content: @Composable () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(start = (8 + indent * 16).dp, top = 10.dp, bottom = 10.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.weight(1f)) { Row(verticalAlignment = Alignment.CenterVertically) { content() } }
        if (selected) Icon(Icons.Default.Check, "Current", tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(18.dp))
    }
}

/** Text color, background, bold / italic / underline, with a preview. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun StyleDialog(name: String, initial: CatStyle?, onDismiss: () -> Unit, onSave: (CatStyle?) -> Unit) {
    var st by remember { mutableStateOf(initial ?: CatStyle()) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Style “$name”") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    buildAnnotatedString { withStyle(styleSpan(st)) { append(name) } },
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
                        .background(styleBg(st) ?: MaterialTheme.colorScheme.surfaceVariant)
                        .padding(12.dp),
                )
                Text("Text color", style = MaterialTheme.typography.labelLarge)
                Swatches(st.fg) { st = st.copy(fg = it) }
                Text("Background", style = MaterialTheme.typography.labelLarge)
                Swatches(st.bg) { st = st.copy(bg = it) }
                FlowRow(verticalArrangement = Arrangement.Center) {
                    Toggle("Bold", st.bold) { st = st.copy(bold = it) }
                    Toggle("Italic", st.italic) { st = st.copy(italic = it) }
                    Toggle("Underline", st.underline) { st = st.copy(underline = it) }
                }
            }
        },
        confirmButton = { TextButton(onClick = { onSave(st.takeUnless { it.isEmpty }) }) { Text("Save") } },
        dismissButton = {
            Row {
                TextButton(onClick = { onSave(null) }) { Text("Clear") }
                TextButton(onClick = onDismiss) { Text("Cancel") }
            }
        },
    )
}

@Composable
private fun Toggle(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.clickable { onChange(!checked) }.padding(end = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked, onChange)
        Text(label)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Swatches(value: String?, onPick: (String?) -> Unit) {
    var custom by remember { mutableStateOf(value?.takeIf { it !in NAMED_COLORS.values } ?: "") }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Swatch(null, value == null) { onPick(null) }
        for (hex in NAMED_COLORS.values) Swatch(hex, value.equals(hex, ignoreCase = true)) { onPick(hex) }
    }
    OutlinedTextField(
        value = custom,
        onValueChange = { s ->
            custom = s.trim()
            val hex = if (custom.startsWith("#")) custom else "#$custom"
            if (parseColor(hex) != null) onPick(hex.lowercase())
        },
        placeholder = { Text("#rrggbb") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii),
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun Swatch(hex: String?, selected: Boolean, onClick: () -> Unit) {
    val ring = if (selected) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.outlineVariant
    Box(
        Modifier.size(30.dp).clip(CircleShape)
            .background(parseColor(hex) ?: MaterialTheme.colorScheme.surface)
            .border(if (selected) 3.dp else 1.dp, ring, CircleShape)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (hex == null) Text("–", color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
fun RoomDialog(title: String, name: String, topic: String, confirm: String, onDismiss: () -> Unit, onSave: (String, String) -> Unit) {
    var n by remember { mutableStateOf(name) }
    var t by remember { mutableStateOf(topic) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(n, { n = it }, label = { Text("Name") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(t, { t = it }, label = { Text("Topic") }, minLines = 2, modifier = Modifier.fillMaxWidth())
            }
        },
        confirmButton = { TextButton(enabled = n.isNotBlank(), onClick = { onSave(n, t) }) { Text(confirm) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun ThemeDialog(themes: List<ServerTheme>, current: String?, onDismiss: () -> Unit, onPick: (ServerTheme?) -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Theme") },
        text = {
            LazyColumn(Modifier.heightIn(max = 420.dp)) {
                item { ThemeRow("System", null, current == null) { onPick(null) } }
                // "default" has no colors: it's the same as following the system.
                items(themes.filter { it.colors.isNotEmpty() }, key = { it.name }) { t ->
                    ThemeRow(t.name, t, current == t.name) { onPick(t) }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Done") } },
    )
}

@Composable
private fun ThemeRow(label: String, theme: ServerTheme?, selected: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(selected, onClick)
        Text(label, Modifier.weight(1f))
        if (theme != null) Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
            for (role in listOf("bg", "fg", "border_active", "accent", "pending", "done")) {
                parseColor(theme.colors[role])?.let {
                    Box(Modifier.size(14.dp).clip(CircleShape).background(it).border(1.dp, MaterialTheme.colorScheme.outlineVariant, CircleShape))
                }
            }
        }
    }
}

/** The server address and who we are; [first] for the setup on first launch. */
@Composable
fun SettingsDialog(model: AppModel, first: Boolean, onDismiss: () -> Unit) {
    var server by remember { mutableStateOf(model.server) }
    var agent by remember { mutableStateOf(model.agent) }
    var error by remember { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = { if (!first) onDismiss() },
        title = { Text(if (first) "Connect to your todo server" else "Settings") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(
                    server, { server = it; error = null },
                    label = { Text("Server") },
                    placeholder = { Text("http://host:27183") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    agent, { agent = it; error = null },
                    label = { Text("Your name in rooms") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Text(
                    "Claims, messages and room events carry this name. There's no login: anyone who can reach the server can use it.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                HorizontalDivider(Modifier.padding(top = 4.dp), color = Color.Transparent)
            }
        },
        confirmButton = {
            TextButton(onClick = {
                error = model.configure(server, agent)
                if (error == null) onDismiss()
            }) { Text(if (first) "Connect" else "Save") }
        },
        dismissButton = if (first) null else ({ TextButton(onClick = onDismiss) { Text("Cancel") } }),
    )
}
