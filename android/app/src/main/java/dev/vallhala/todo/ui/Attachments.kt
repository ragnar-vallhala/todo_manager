package dev.vallhala.todo.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BrokenImage
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import coil3.request.crossfade
import dev.vallhala.todo.durationLabel

/** A round avatar with the name's initials, in the name's own color. */
@Composable
fun Avatar(name: String, size: Dp, modifier: Modifier = Modifier) {
    Box(
        modifier.size(size).clip(CircleShape).background(avatarColor(name)),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            initials(name),
            color = Color.White,
            fontWeight = FontWeight.SemiBold,
            fontSize = (size.value * 0.4f).sp,
            maxLines = 1,
        )
    }
}

/** An image (a URL or a local file), cropped to fill, with a quiet placeholder while it loads. */
@Composable
fun Thumb(data: Any, modifier: Modifier = Modifier, contentDescription: String? = null) {
    val ctx = LocalContext.current
    var failed by remember(data) { mutableStateOf(false) }
    Box(modifier.background(MaterialTheme.colorScheme.surfaceVariant), contentAlignment = Alignment.Center) {
        AsyncImage(
            model = remember(data) { ImageRequest.Builder(ctx).data(data).crossfade(true).build() },
            contentDescription = contentDescription,
            contentScale = ContentScale.Crop,
            onError = { failed = true },
            onSuccess = { failed = false },
            modifier = Modifier.fillMaxSize(),
        )
        if (failed) Icon(Icons.Default.BrokenImage, "Couldn't load the image", tint = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/** One image (or video, shown by a frame) to lay out: what to load, and its size in pixels if known (0 if not). */
data class Pic(val data: Any, val width: Int, val height: Int, val name: String, val video: Boolean = false, val duration: Double = 0.0)

/** A picture's thumbnail; a video's has a play button and its length on it. */
@Composable
fun MediaThumb(p: Pic, modifier: Modifier = Modifier, small: Boolean = false) {
    Box(modifier) {
        Thumb(p.data, Modifier.fillMaxSize(), p.name)
        if (p.video) {
            Box(
                Modifier.align(Alignment.Center).size(if (small) 28.dp else 46.dp).clip(CircleShape).background(Color.Black.copy(alpha = 0.5f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Default.PlayArrow, "Video", Modifier.size(if (small) 18.dp else 30.dp), tint = Color.White)
            }
            if (p.duration > 0) Row(
                Modifier.align(Alignment.BottomStart).padding(5.dp).clip(RoundedCornerShape(6.dp))
                    .background(Color.Black.copy(alpha = 0.5f)).padding(horizontal = 5.dp, vertical = 1.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(Icons.Default.Videocam, null, Modifier.size(12.dp), tint = Color.White)
                Spacer(Modifier.width(3.dp))
                Text(durationLabel(p.duration), color = Color.White, style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

/**
 * Images as in a chat: one at its own aspect ratio (within reason, so the
 * bubble doesn't jump when it loads), several as a grid of squares, at most
 * four with "+N" on the last.
 */
@Composable
fun ImageGrid(
    pics: List<Pic>,
    width: Dp,
    corner: Dp,
    onClick: (Int) -> Unit,
    onLongClick: (() -> Unit)? = null,
    overlay: @Composable BoxScope.() -> Unit = {},
) {
    val shape = RoundedCornerShape(corner)
    Box(Modifier.width(width).clip(shape)) {
        if (pics.size == 1) {
            val p = pics[0]
            val ratio = if (p.width > 0 && p.height > 0) (p.width.toFloat() / p.height).coerceIn(0.62f, 1.9f) else 4f / 3f
            MediaThumb(p, Modifier.fillMaxWidth().aspectRatio(ratio).tap({ onClick(0) }, onLongClick))
        } else {
            val shown = pics.take(4)
            val gap = 3.dp
            Column(verticalArrangement = Arrangement.spacedBy(gap)) {
                for (row in shown.indices.chunked(2)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(gap)) {
                        for (i in row) {
                            // A lone third image takes the whole row.
                            val w = if (row.size == 1) width else (width - gap) / 2
                            Box(Modifier.width(w).aspectRatio(if (row.size == 1) 2f else 1f)) {
                                MediaThumb(shown[i], Modifier.fillMaxSize().tap({ onClick(i) }, onLongClick), small = true)
                                if (i == 3 && pics.size > 4) {
                                    Box(
                                        Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.45f)).tap({ onClick(i) }, onLongClick),
                                        contentAlignment = Alignment.Center,
                                    ) {
                                        Text("+${pics.size - 4}", color = Color.White, style = MaterialTheme.typography.headlineSmall)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        overlay()
    }
}

@OptIn(ExperimentalFoundationApi::class)
private fun Modifier.tap(onClick: () -> Unit, onLongClick: (() -> Unit)?) =
    combinedClickable(onClick = onClick, onLongClick = onLongClick)

/** A PDF (or other file) as a card: icon, name, size; a spinner while it's being fetched to open. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun FileCard(
    name: String,
    detail: String,
    busy: Boolean,
    container: Color,
    content: Color,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
) {
    Surface(
        color = container,
        contentColor = content,
        shape = RoundedCornerShape(12.dp),
        modifier = modifier.widthIn(min = 200.dp)
            .clip(RoundedCornerShape(12.dp))
            .then(if (onClick != null) Modifier.combinedClickable(onClick = onClick, onLongClick = onLongClick) else Modifier),
    ) {
        Row(Modifier.padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier.size(42.dp).clip(RoundedCornerShape(10.dp)).background(Color(0xFFE5483A)),
                contentAlignment = Alignment.Center,
            ) {
                if (busy) CircularProgressIndicator(Modifier.size(22.dp), color = Color.White, strokeWidth = 2.5.dp)
                else Icon(Icons.Default.PictureAsPdf, null, tint = Color.White)
            }
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f, fill = false)) {
                Text(name, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium, maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(detail, style = MaterialTheme.typography.labelSmall, color = content.copy(alpha = 0.7f))
            }
        }
    }
}

/** A big icon, a line and a hint, for an empty list. */
@Composable
fun EmptyState(icon: ImageVector, title: String, body: String, modifier: Modifier = Modifier, action: (@Composable () -> Unit)? = null) {
    Column(
        modifier.fillMaxWidth().padding(horizontal = 40.dp, vertical = 56.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.size(96.dp).clip(CircleShape).background(MaterialTheme.colorScheme.secondaryContainer),
            contentAlignment = Alignment.Center,
        ) {
            Icon(icon, null, Modifier.size(44.dp), tint = MaterialTheme.colorScheme.onSecondaryContainer)
        }
        Spacer(Modifier.height(20.dp))
        Text(title, style = MaterialTheme.typography.titleMedium, textAlign = TextAlign.Center)
        Spacer(Modifier.height(6.dp))
        Text(body, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
        if (action != null) {
            Spacer(Modifier.height(18.dp))
            action()
        }
    }
}
