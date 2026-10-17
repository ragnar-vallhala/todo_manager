package dev.vallhala.todo.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import dev.vallhala.todo.CatStyle
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
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

// ── Chat helpers ──

/** Colors for names and avatars, light-theme tone then dark-theme tone; picked by a stable hash of the name. */
private val NAME_TONES = listOf(
    0xFFC62828 to 0xFFFF8A80, 0xFFAD1457 to 0xFFF48FB1, 0xFF6A1B9A to 0xFFCE93D8, 0xFF4527A0 to 0xFFB39DDB,
    0xFF1565C0 to 0xFF90CAF9, 0xFF00838F to 0xFF80DEEA, 0xFF00695C to 0xFF80CBC4, 0xFF2E7D32 to 0xFFA5D6A7,
    0xFF9E6A00 to 0xFFFFD180, 0xFFD84315 to 0xFFFFAB91, 0xFF5D4037 to 0xFFD7B8A8, 0xFF455A64 to 0xFFB0BEC5,
)

private fun tone(name: String) = NAME_TONES[(name.lowercase().hashCode() and 0x7fffffff) % NAME_TONES.size]

/** A name's own color, readable on the theme's surfaces. */
fun nameColor(name: String, dark: Boolean) = Color(if (dark) tone(name).second else tone(name).first)

/** A name's avatar fill (white initials read on it in both themes). */
fun avatarColor(name: String) = Color(tone(name).first)

/** "RF" for "rover-firmware", "C" for "claude". */
fun initials(name: String): String {
    val words = name.split(Regex("[^\\p{L}\\p{N}]+")).filter { it.isNotEmpty() }
    return when {
        words.isEmpty() -> name.take(1).uppercase().ifEmpty { "?" }
        words.size == 1 -> words[0].take(1).uppercase()
        else -> (words[0].take(1) + words[1].take(1)).uppercase()
    }
}

fun localDate(seconds: Long): LocalDate = Instant.ofEpochSecond(seconds).atZone(ZoneId.systemDefault()).toLocalDate()

/** "Today", "Yesterday", "Monday", "3 March", "3 March 2025". */
fun dayLabel(d: LocalDate): String {
    val today = LocalDate.now()
    val diff = ChronoUnit.DAYS.between(d, today)
    return when {
        diff == 0L -> "Today"
        diff == 1L -> "Yesterday"
        diff in 2..6 -> d.dayOfWeek.getDisplayName(TextStyle.FULL, Locale.getDefault())
        d.year == today.year -> d.format(DateTimeFormatter.ofPattern("d MMMM"))
        else -> d.format(DateTimeFormatter.ofPattern("d MMMM yyyy"))
    }
}

/** The clock format the phone is set to (24-hour or not). */
@Composable
fun rememberClock(): (Long) -> String {
    val ctx = LocalContext.current
    val fmt = remember(ctx) {
        DateTimeFormatter.ofPattern(if (android.text.format.DateFormat.is24HourFormat(ctx)) "HH:mm" else "h:mm a")
    }
    return remember(fmt) { { s: Long -> Instant.ofEpochSecond(s).atZone(ZoneId.systemDefault()).format(fmt) } }
}

/** For the room list: the time today, "Yesterday", a weekday this week, else a short date. */
fun listTime(seconds: Long, clock: (Long) -> String): String {
    val d = localDate(seconds)
    val diff = ChronoUnit.DAYS.between(d, LocalDate.now())
    return when {
        diff <= 0L -> clock(seconds)
        diff == 1L -> "Yesterday"
        diff in 2..6 -> d.dayOfWeek.getDisplayName(TextStyle.SHORT, Locale.getDefault())
        else -> d.format(DateTimeFormatter.ofLocalizedDate(FormatStyle.SHORT))
    }
}

private val URL = Regex("""(?i)\b(?:https?://|www\.)[^\s<>"']+""")
private val MENTION = Regex("""(?<![\w@])@[\p{L}\p{N}_.-]+""")
private val TODO_REF = Regex("""(?<![\w#&])#(\d{1,6})\b""")

/**
 * Message text with links clickable, @mentions highlighted (ours more so) and
 * #N todo references opening the todo, when [onTodo] says it exists.
 */
fun richText(
    text: String,
    link: Color,
    mention: Color,
    me: String,
    onTodo: ((Int) -> Boolean)? = null,
): AnnotatedString = buildAnnotatedString {
    data class Span(val range: IntRange, val kind: Int, val value: String)
    val spans = ArrayList<Span>()
    for (m in URL.findAll(text)) {
        // Leave trailing punctuation outside the link.
        val v = m.value.trimEnd('.', ',', ';', ':', '!', '?', ')', ']', '}', '\'', '"')
        spans += Span(m.range.first until m.range.first + v.length, 0, v)
    }
    fun free(r: IntRange) = spans.none { it.range.first <= r.last && r.first <= it.range.last }
    for (m in MENTION.findAll(text)) {
        val v = m.value.trimEnd('.', '-')
        val r = m.range.first until m.range.first + v.length
        if (free(r)) spans += Span(r, 1, v)
    }
    if (onTodo != null) for (m in TODO_REF.findAll(text)) if (free(m.range)) spans += Span(m.range, 2, m.groupValues[1])
    spans.sortBy { it.range.first }
    var at = 0
    for (s in spans) {
        append(text.substring(at, s.range.first))
        val shown = text.substring(s.range.first, s.range.last + 1)
        when (s.kind) {
            0 -> {
                val url = if (s.value.startsWith("www.", ignoreCase = true)) "https://${s.value}" else s.value
                withLink(LinkAnnotation.Url(url, TextLinkStyles(SpanStyle(color = link, textDecoration = TextDecoration.Underline)))) {
                    append(shown)
                }
            }
            1 -> {
                val isMe = s.value.drop(1).equals(me, ignoreCase = true)
                withStyle(
                    SpanStyle(
                        color = mention,
                        fontWeight = FontWeight.SemiBold,
                        background = if (isMe) mention.copy(alpha = 0.16f) else Color.Unspecified,
                    ),
                ) { append(shown) }
            }
            else -> {
                val id = s.value.toInt()
                withLink(
                    LinkAnnotation.Clickable("todo$id", TextLinkStyles(SpanStyle(color = link, fontWeight = FontWeight.Medium))) {
                        onTodo?.invoke(id)
                    },
                ) { append(shown) }
            }
        }
        at = s.range.last + 1
    }
    append(text.substring(at))
}
