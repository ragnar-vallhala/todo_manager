package dev.vallhala.todo.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalContext

/** Colors the todo views use beyond Material's: the same roles as the server's themes. */
@Immutable
data class Extra(
    val muted: Color,
    val done: Color,
    val warn: Color,
    val danger: Color,
    val category: Color,
    val accent: Color,
)

val LocalExtra = staticCompositionLocalOf {
    Extra(Color.Gray, Color(0xFF3F9E5A), Color(0xFFC9871F), Color(0xFFD0453A), Color(0xFF4F7FE0), Color(0xFF4F7FE0))
}

fun parseColor(hex: String?): Color? {
    if (hex == null || !hex.startsWith("#") || hex.length != 7) return null
    return hex.drop(1).toLongOrNull(16)?.let { Color(0xFF000000 or it) }
}

/** The named colors categories can use (same as the CLI and web UI). */
val NAMED_COLORS = linkedMapOf(
    "red" to "#e5534b", "orange" to "#e08a3c", "yellow" to "#c9a227", "green" to "#57a65a",
    "teal" to "#2aa198", "blue" to "#4f7fe0", "purple" to "#9a6ce0", "pink" to "#d9579b",
    "brown" to "#a0724a", "gray" to "#8a8f98",
)

fun isDark(c: Color) = c.luminance() < 0.18f

@Composable
fun TodoTheme(colors: Map<String, String>, content: @Composable () -> Unit) {
    val bg = parseColor(colors["bg"])
    val dark = bg?.let(::isDark) ?: isSystemInDarkTheme()
    val base: ColorScheme = when {
        bg != null -> if (dark) darkColorScheme() else lightColorScheme()
        Build.VERSION.SDK_INT >= 31 -> if (dark) dynamicDarkColorScheme(LocalContext.current) else dynamicLightColorScheme(LocalContext.current)
        dark -> darkColorScheme(primary = Color(0xFF8FB0F0), background = Color(0xFF1E2126), surface = Color(0xFF1E2126))
        else -> lightColorScheme(primary = Color(0xFF3D68C4))
    }
    val scheme = if (bg == null) base else themed(base, colors, bg, dark)
    val extra = Extra(
        muted = parseColor(colors["muted"]) ?: scheme.onSurfaceVariant,
        done = parseColor(colors["done"]) ?: if (dark) Color(0xFF6CC488) else Color(0xFF2E8049),
        warn = parseColor(colors["pending"]) ?: if (dark) Color(0xFFE5B158) else Color(0xFFA86A0C),
        danger = parseColor(colors["error"]) ?: scheme.error,
        category = parseColor(colors["category"]) ?: scheme.primary,
        accent = parseColor(colors["accent"]) ?: scheme.primary,
    )
    CompositionLocalProvider(LocalExtra provides extra) {
        MaterialTheme(colorScheme = scheme, content = content)
    }
}

/** A Material scheme from a server theme's roles, filling in what the theme doesn't set. */
private fun themed(base: ColorScheme, c: Map<String, String>, bg: Color, dark: Boolean): ColorScheme {
    val fg = parseColor(c["fg"]) ?: if (dark) Color(0xFFE7E5E0) else Color(0xFF1E2126)
    val accent = parseColor(c["border_active"]) ?: parseColor(c["accent"]) ?: base.primary
    val onAccent = if (isDark(accent)) Color.White else Color.Black
    val sel = parseColor(c["selection_bg"]) ?: lerp(bg, accent, 0.35f)
    val selFg = parseColor(c["selection_fg"]) ?: fg
    val raised = lerp(bg, fg, 0.06f)
    return base.copy(
        primary = accent, onPrimary = onAccent,
        primaryContainer = sel, onPrimaryContainer = selFg,
        secondary = parseColor(c["accent"]) ?: accent, onSecondary = onAccent,
        secondaryContainer = parseColor(c["selection_bg_inactive"]) ?: lerp(bg, fg, 0.12f), onSecondaryContainer = fg,
        tertiary = parseColor(c["category"]) ?: accent,
        background = bg, onBackground = fg,
        surface = bg, onSurface = fg,
        surfaceVariant = lerp(bg, fg, 0.10f), onSurfaceVariant = parseColor(c["muted"]) ?: lerp(bg, fg, 0.6f),
        surfaceContainerLowest = bg, surfaceContainerLow = raised, surfaceContainer = raised,
        surfaceContainerHigh = lerp(bg, fg, 0.09f), surfaceContainerHighest = lerp(bg, fg, 0.12f),
        surfaceBright = lerp(bg, fg, 0.1f), surfaceDim = bg,
        inverseSurface = fg, inverseOnSurface = bg,
        outline = parseColor(c["border"]) ?: lerp(bg, fg, 0.3f),
        outlineVariant = lerp(bg, fg, 0.15f),
        error = parseColor(c["error"]) ?: base.error,
    )
}
