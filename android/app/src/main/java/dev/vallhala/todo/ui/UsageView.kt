package dev.vallhala.todo.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Usage
import dev.vallhala.todo.UsageDevice
import kotlinx.coroutines.delay

/** The server's latest usage reports, refetched every 30 s while this is on screen. */
@Composable
fun pollUsage(model: AppModel): State<Usage?> {
    val state = remember { mutableStateOf<Usage?>(null) }
    LaunchedEffect(Unit) {
        while (true) {
            runCatching { model.usage() }.onSuccess { state.value = it }
            delay(30_000)
        }
    }
    return state
}

/** Green below 70 %, yellow from 70, red from 90: the same as the web UI and `todo usage`. */
@Composable
private fun usageColor(pct: Double?): Color {
    val x = LocalExtra.current
    return when {
        pct == null -> x.muted
        pct >= 90 -> x.danger
        pct >= 70 -> x.warn
        else -> x.done
    }
}

/** The highest percentage over the devices that report it. */
private fun worst(u: Usage, f: (UsageDevice) -> Double?) = u.devices.mapNotNull(f).maxOrNull()

private fun pct(p: Double?) = if (p == null) "–" else "${p.toInt()}%"

private fun resetIn(at: Long?, now: Long): String {
    if (at == null) return ""
    val s = at - now
    if (s <= 0) return "now"
    val m = s / 60
    return when {
        m < 60 -> "${m}m"
        m < 48 * 60 -> "${m / 60}h ${m % 60}m"
        else -> "${m / 1440}d ${m % 1440 / 60}h"
    }
}

/** One line under the screen title: the worst 5 h and 7 d figures. */
@Composable
fun UsageStrip(usage: Usage?) {
    if (usage == null || usage.devices.isEmpty()) return
    val h5 = worst(usage) { it.fiveHourPct }
    val d7 = worst(usage) { it.sevenDayPct }
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("5h ${pct(h5)}", style = MaterialTheme.typography.labelSmall, color = usageColor(h5), maxLines = 1)
        Text("7d ${pct(d7)}", style = MaterialTheme.typography.labelSmall, color = usageColor(d7), maxLines = 1)
    }
}

@Composable
private fun Meter(label: String, pct: Double?, resetsAt: Long?, now: Long) {
    val c = usageColor(pct)
    Column(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("$label ${pct(pct)}", style = MaterialTheme.typography.bodyMedium, color = c)
            val r = resetIn(resetsAt, now)
            if (r.isNotEmpty()) Text("resets in $r", style = MaterialTheme.typography.labelSmall, color = LocalExtra.current.muted)
        }
        LinearProgressIndicator(
            progress = { ((pct ?: 0.0) / 100).coerceIn(0.0, 1.0).toFloat() },
            color = c,
            trackColor = LocalExtra.current.muted.copy(alpha = 0.25f),
            modifier = Modifier.fillMaxWidth().padding(top = 3.dp),
        )
    }
}

@Composable
fun UsageDialog(usage: Usage?, onDismiss: () -> Unit) {
    val muted = LocalExtra.current.muted
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
        title = { Text("Claude usage") },
        text = {
            Column {
                if (usage == null || usage.devices.isEmpty()) Text("No reports yet.", color = muted)
                else usage.devices.sortedBy { it.device }.forEach { d ->
                    Text(d.device, style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 8.dp))
                    Meter("5 hours", d.fiveHourPct, d.fiveHourResetsAt, usage.now)
                    Meter("7 days", d.sevenDayPct, d.sevenDayResetsAt, usage.now)
                    d.sessions.take(6).forEach { s ->
                        Row(Modifier.fillMaxWidth()) {
                            Text(
                                s.name.ifEmpty { s.project }, style = MaterialTheme.typography.labelSmall,
                                color = muted, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f),
                            )
                            Text(
                                "${s.idx30m.toLong()} / ${s.idxWindow.toLong()}", style = MaterialTheme.typography.labelSmall,
                                fontFamily = FontFamily.Monospace, color = muted, modifier = Modifier.width(110.dp),
                            )
                        }
                    }
                }
            }
        },
    )
}
