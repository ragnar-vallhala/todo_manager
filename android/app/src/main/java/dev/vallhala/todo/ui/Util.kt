package dev.vallhala.todo.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import dev.vallhala.todo.CatStyle
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.time.format.TextStyle
import java.time.temporal.ChronoUnit
import java.util.Locale

val MARKS = mapOf("high" to "!!!", "medium" to "!!", "low" to "!")
val PRIORITY_NAMES = listOf(null to "None", "low" to "Low", "medium" to "Medium", "high" to "High")

fun parseDate(iso: String?): LocalDate? = iso?.let { runCatching { LocalDate.parse(it) }.getOrNull() }

/** Short and relative when near: today, tomorrow, Fri, 3d ago, Oct 5. */
fun dueLabel(iso: String): String {
    val date = parseDate(iso) ?: return iso
    val today = LocalDate.now()
    val diff = ChronoUnit.DAYS.between(today, date)
    return when {
        diff == 0L -> "today"
        diff == 1L -> "tomorrow"
        diff == -1L -> "yesterday"
        diff < 0 -> "${-diff}d ago"
        diff < 7 -> date.dayOfWeek.getDisplayName(TextStyle.SHORT, Locale.getDefault())
        date.year == today.year -> date.format(DateTimeFormatter.ofPattern("MMM d"))
        else -> date.format(DateTimeFormatter.ofPattern("MMM d, yyyy"))
    }
}

fun overdue(iso: String?) = parseDate(iso)?.isBefore(LocalDate.now()) == true

/** "just now", "5m ago", "3h ago", "2d ago". */
fun ago(seconds: Long): String {
    val s = (System.currentTimeMillis() / 1000 - seconds).coerceAtLeast(0)
    return when {
        s < 60 -> "just now"
        s < 3600 -> "${s / 60}m ago"
        s < 86400 -> "${s / 3600}h ago"
        else -> "${s / 86400}d ago"
    }
}

/** A category style's text color: its own, else whichever reads better on its background. */
fun styleText(st: CatStyle?): Color? {
    if (st == null) return null
    parseColor(st.fg)?.let { return it }
    val bg = parseColor(st.bg) ?: return null
    return if (isDark(bg)) Color(0xFFF4F4F0) else Color(0xFF1E2126)
}

fun styleBg(st: CatStyle?): Color? = parseColor(st?.bg)

fun styleSpan(st: CatStyle?) = SpanStyle(
    color = styleText(st) ?: Color.Unspecified,
    fontWeight = if (st?.bold == true) FontWeight.SemiBold else null,
    fontStyle = if (st?.italic == true) FontStyle.Italic else null,
    textDecoration = if (st?.underline == true) TextDecoration.Underline else null,
)
