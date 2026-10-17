package dev.vallhala.todo.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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
    val dark: Boolean = false,
    /** The room log's backdrop, and the bubbles on it: ours, then everyone else's. */
    val chatBg: Color = Color.Unspecified,
    val mine: Color = Color.Unspecified,
    val onMine: Color = Color.Unspecified,
    val other: Color = Color.Unspecified,
    val onOther: Color = Color.Unspecified,
)

val LocalExtra = staticCompositionLocalOf {
    Extra(Color.Gray, Color(0xFF3F9E5A), Color(0xFFC9871F), Color(0xFFD0453A), Color(0xFF4F7FE0), Color(0xFF4F7FE0))
}

// The palette when there's no dynamic color (before Android 12): a calm teal.
private val LightPalette = lightColorScheme(
    primary = Color(0xFF006A60), onPrimary = Color.White,
    primaryContainer = Color(0xFF9EF2E4), onPrimaryContainer = Color(0xFF00201C),
    secondary = Color(0xFF4A635F), onSecondary = Color.White,
    secondaryContainer = Color(0xFFCCE8E2), onSecondaryContainer = Color(0xFF05201C),
    tertiary = Color(0xFF456179), onTertiary = Color.White,
    tertiaryContainer = Color(0xFFCCE5FF), onTertiaryContainer = Color(0xFF001E31),
    error = Color(0xFFBA1A1A), onError = Color.White,
    errorContainer = Color(0xFFFFDAD6), onErrorContainer = Color(0xFF410002),
    background = Color(0xFFF4FBF8), onBackground = Color(0xFF161D1C),
    surface = Color(0xFFF4FBF8), onSurface = Color(0xFF161D1C),
    surfaceVariant = Color(0xFFDAE5E1), onSurfaceVariant = Color(0xFF3F4947),
    outline = Color(0xFF6F7977), outlineVariant = Color(0xFFBEC9C6),
    inverseSurface = Color(0xFF2B3230), inverseOnSurface = Color(0xFFECF2EF), inversePrimary = Color(0xFF82D5C8),
    surfaceContainerLowest = Color.White, surfaceContainerLow = Color(0xFFEEF5F2),
    surfaceContainer = Color(0xFFE9EFEC), surfaceContainerHigh = Color(0xFFE3EAE7),
    surfaceContainerHighest = Color(0xFFDDE4E1), surfaceDim = Color(0xFFD5DBD9), surfaceBright = Color(0xFFF4FBF8),
)

private val DarkPalette = darkColorScheme(
    primary = Color(0xFF82D5C8), onPrimary = Color(0xFF003731),
    primaryContainer = Color(0xFF005048), onPrimaryContainer = Color(0xFF9EF2E4),
    secondary = Color(0xFFB1CCC6), onSecondary = Color(0xFF1C3531),
    secondaryContainer = Color(0xFF334B47), onSecondaryContainer = Color(0xFFCCE8E2),
    tertiary = Color(0xFFADCAE6), onTertiary = Color(0xFF153349),
    tertiaryContainer = Color(0xFF2D4961), onTertiaryContainer = Color(0xFFCCE5FF),
    error = Color(0xFFFFB4AB), onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A), onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF0E1513), onBackground = Color(0xFFDDE4E1),
    surface = Color(0xFF0E1513), onSurface = Color(0xFFDDE4E1),
    surfaceVariant = Color(0xFF3F4947), onSurfaceVariant = Color(0xFFBEC9C6),
    outline = Color(0xFF899390), outlineVariant = Color(0xFF3F4947),
    inverseSurface = Color(0xFFDDE4E1), inverseOnSurface = Color(0xFF2B3230), inversePrimary = Color(0xFF006A60),
    surfaceContainerLowest = Color(0xFF090F0E), surfaceContainerLow = Color(0xFF161D1C),
    surfaceContainer = Color(0xFF1A2120), surfaceContainerHigh = Color(0xFF252B2A),
    surfaceContainerHighest = Color(0xFF303635), surfaceDim = Color(0xFF0E1513), surfaceBright = Color(0xFF343B39),
)

private val Base = Typography()

/** Material's type scale, a little firmer in the titles and looser in body text. */
private val AppTypography = Typography(
    displaySmall = Base.displaySmall.copy(fontWeight = FontWeight.SemiBold),
    headlineMedium = Base.headlineMedium.copy(fontWeight = FontWeight.SemiBold),
    headlineSmall = Base.headlineSmall.copy(fontWeight = FontWeight.SemiBold),
    titleLarge = Base.titleLarge.copy(fontWeight = FontWeight.SemiBold, letterSpacing = 0.sp),
    titleMedium = Base.titleMedium.copy(fontWeight = FontWeight.SemiBold),
    titleSmall = Base.titleSmall.copy(fontWeight = FontWeight.SemiBold),
    bodyLarge = Base.bodyLarge.copy(lineHeight = 23.sp, letterSpacing = 0.2.sp),
    bodyMedium = Base.bodyMedium.copy(lineHeight = 20.sp, letterSpacing = 0.15.sp),
    labelLarge = Base.labelLarge.copy(fontWeight = FontWeight.SemiBold),
)

private val AppShapes = Shapes(
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(22.dp),
    extraLarge = RoundedCornerShape(28.dp),
)

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
        dark -> DarkPalette
        else -> LightPalette
    }
    val scheme = if (bg == null) base else themed(base, colors, bg, dark)
    val extra = Extra(
        muted = parseColor(colors["muted"]) ?: scheme.onSurfaceVariant,
        done = parseColor(colors["done"]) ?: if (dark) Color(0xFF6CC488) else Color(0xFF2E8049),
        warn = parseColor(colors["pending"]) ?: if (dark) Color(0xFFE5B158) else Color(0xFFA86A0C),
        danger = parseColor(colors["error"]) ?: scheme.error,
        category = parseColor(colors["category"]) ?: scheme.primary,
        accent = parseColor(colors["accent"]) ?: scheme.primary,
        dark = dark,
        chatBg = if (dark) scheme.surfaceContainerLowest else scheme.surfaceContainer,
        mine = scheme.primaryContainer,
        onMine = scheme.onPrimaryContainer,
        other = if (dark) scheme.surfaceContainerHigh else scheme.surfaceContainerLowest,
        onOther = scheme.onSurface,
    )
    CompositionLocalProvider(LocalExtra provides extra) {
        MaterialTheme(colorScheme = scheme, typography = AppTypography, shapes = AppShapes, content = content)
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
