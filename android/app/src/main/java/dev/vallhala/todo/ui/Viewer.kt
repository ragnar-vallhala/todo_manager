package dev.vallhala.todo.ui

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import android.view.WindowManager
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import coil3.compose.AsyncImagePainter
import coil3.compose.SubcomposeAsyncImage
import coil3.compose.SubcomposeAsyncImageContent
import coil3.request.ImageRequest
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Gallery

/** Images full screen: swipe between them, pinch or double-tap to zoom, save to the gallery or share. */
@Composable
fun ImageViewer(model: AppModel, g: Gallery) {
    val items = g.items
    val pager = rememberPagerState(initialPage = g.start) { items.size }
    val zoomed = remember { mutableStateMapOf<Int, Boolean>() }
    var chrome by remember { mutableStateOf(true) }
    val clock = rememberClock()
    val current = items.getOrNull(pager.currentPage)
    val askWrite = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (ok) current?.let { model.saveFile(it.attachment) } else model.notice("Can't save without storage permission")
    }

    Dialog(
        onDismissRequest = { model.closeGallery() },
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        FullScreenWindow()
        Box(Modifier.fillMaxSize().background(Color.Black)) {
            HorizontalPager(
                state = pager,
                userScrollEnabled = zoomed[pager.currentPage] != true,
                beyondViewportPageCount = 1,
                key = { "${items[it].attachment.id}/$it" },
                modifier = Modifier.fillMaxSize(),
            ) { page ->
                ZoomableImage(
                    model.fileUrl(items[page].attachment.id),
                    onTap = { chrome = !chrome },
                    onZoom = { zoomed[page] = it },
                )
            }

            AnimatedVisibility(chrome, enter = fadeIn(), exit = fadeOut(), modifier = Modifier.align(Alignment.TopCenter)) {
                Row(
                    Modifier.fillMaxWidth()
                        .background(Brush.verticalGradient(listOf(Color.Black.copy(alpha = 0.7f), Color.Transparent)))
                        .windowInsetsPadding(WindowInsets.statusBars)
                        .padding(4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    IconButton(onClick = { model.closeGallery() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Close", tint = Color.White) }
                    Column(Modifier.weight(1f).padding(start = 4.dp)) {
                        Text(
                            current?.author ?: current?.attachment?.name ?: "",
                            color = Color.White,
                            style = MaterialTheme.typography.titleMedium,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        current?.time?.let {
                            Text("${dayLabel(localDate(it))}, ${clock(it)}", color = Color.White.copy(alpha = 0.75f), style = MaterialTheme.typography.bodySmall)
                        }
                    }
                    if (model.fetching != null) CircularProgressIndicator(Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp)
                    IconButton(onClick = { current?.let { model.shareFile(it.attachment) } }) { Icon(Icons.Default.Share, "Share", tint = Color.White) }
                    IconButton(onClick = {
                        val a = current?.attachment ?: return@IconButton
                        if (Build.VERSION.SDK_INT < 29) askWrite.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE) else model.saveFile(a)
                    }) { Icon(Icons.Default.Download, "Save to gallery", tint = Color.White) }
                }
            }

            if (items.size > 1) AnimatedVisibility(chrome, enter = fadeIn(), exit = fadeOut(), modifier = Modifier.align(Alignment.BottomCenter)) {
                Box(
                    Modifier.fillMaxWidth()
                        .background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.6f))))
                        .windowInsetsPadding(WindowInsets.navigationBars)
                        .padding(16.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text("${pager.currentPage + 1} of ${items.size}", color = Color.White, style = MaterialTheme.typography.labelLarge)
                }
            }
        }
    }
}

/**
 * An image that fits the screen, zooms with a pinch (around the fingers) or a
 * double tap, and pans while zoomed. Unzoomed, a one-finger drag is left
 * alone so the pager can swipe.
 */
@Composable
private fun ZoomableImage(url: String, onTap: () -> Unit, onZoom: (Boolean) -> Unit) {
    val ctx = LocalContext.current
    var scale by remember { mutableFloatStateOf(1f) }
    var offset by remember { mutableStateOf(Offset.Zero) }
    BoxWithConstraints(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        val w = constraints.maxWidth.toFloat()
        val h = constraints.maxHeight.toFloat()
        fun clamp(o: Offset, s: Float): Offset {
            val mx = w * (s - 1) / 2
            val my = h * (s - 1) / 2
            return Offset(o.x.coerceIn(-mx, mx), o.y.coerceIn(-my, my))
        }
        fun set(s: Float, o: Offset) {
            scale = s
            offset = clamp(o, s)
            onZoom(s > 1.01f)
        }
        SubcomposeAsyncImage(
            model = remember(url) { ImageRequest.Builder(ctx).data(url).size(2560).build() },
            contentDescription = null,
            contentScale = ContentScale.Fit,
            modifier = Modifier.fillMaxSize()
                .pointerInput(Unit) {
                    detectTapGestures(
                        onTap = { onTap() },
                        onDoubleTap = { p ->
                            if (scale > 1.01f) set(1f, Offset.Zero)
                            else {
                                val s = 2.5f
                                // Keep the tapped point where it was.
                                val c = Offset(w / 2, h / 2)
                                set(s, (c - p) * (s - 1))
                            }
                        },
                    )
                }
                .pointerInput(Unit) {
                    awaitEachGesture {
                        awaitFirstDown(requireUnconsumed = false)
                        do {
                            val event = awaitPointerEvent()
                            val fingers = event.changes.count { it.pressed }
                            if (fingers >= 2 || scale > 1.01f) {
                                val zoom = event.calculateZoom()
                                val pan = event.calculatePan()
                                val centroid = event.calculateCentroid(useCurrent = true)
                                val s = (scale * zoom).coerceIn(1f, 5f)
                                val c = Offset(w / 2, h / 2)
                                // Zoom about the fingers: the point under them stays put.
                                val o = if (centroid == Offset.Unspecified) offset + pan
                                else (offset + c - centroid) * (s / scale) - (c - centroid) + pan
                                set(s, o)
                                event.changes.forEach { if (it.positionChanged()) it.consume() }
                            }
                        } while (event.changes.any { it.pressed })
                    }
                }
                .graphicsLayer {
                    scaleX = scale
                    scaleY = scale
                    translationX = offset.x
                    translationY = offset.y
                },
        ) {
            when (painter.state.collectAsState().value) {
                is AsyncImagePainter.State.Loading, AsyncImagePainter.State.Empty -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = Color.White)
                }
                is AsyncImagePainter.State.Error -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text("Couldn't load the image", color = Color.White)
                }
                else -> SubcomposeAsyncImageContent()
            }
        }
    }
}

/** Lets the dialog's window cover the status bar and the cutout too, undimmed, with light bar icons. */
@Composable
private fun FullScreenWindow() {
    val view = LocalView.current
    SideEffect {
        val w = (view.parent as? DialogWindowProvider)?.window ?: return@SideEffect
        w.setDimAmount(0f)
        w.setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT)
        WindowCompat.setDecorFitsSystemWindows(w, false)
        if (Build.VERSION.SDK_INT >= 28) {
            w.attributes = w.attributes.apply {
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
        }
        @Suppress("DEPRECATION")
        w.statusBarColor = android.graphics.Color.TRANSPARENT
        @Suppress("DEPRECATION")
        w.navigationBarColor = android.graphics.Color.TRANSPARENT
        WindowCompat.getInsetsController(w, view).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }
    }
}
