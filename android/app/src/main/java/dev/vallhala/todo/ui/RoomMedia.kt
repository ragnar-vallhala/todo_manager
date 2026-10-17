package dev.vallhala.todo.ui

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material.icons.filled.PictureAsPdf
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.vallhala.todo.AppModel
import dev.vallhala.todo.Attachment
import dev.vallhala.todo.GalleryItem
import dev.vallhala.todo.Room
import dev.vallhala.todo.RoomFile
import dev.vallhala.todo.sizeLabel
import java.time.format.DateTimeFormatter

/** A room's media (photos and videos by month), docs and links, like a chat app's "Media, links and docs". */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RoomMediaScreen(model: AppModel, room: Room) {
    val st = model.roomMedia?.takeIf { it.roomId == room.id }
    var tab by rememberSaveable { mutableIntStateOf(0) }
    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    navigationIcon = { IconButton(onClick = { model.pop() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") } },
                    title = {
                        Column {
                            Text("Media, links and docs")
                            Text(room.name, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    },
                )
                PrimaryTabRow(selectedTabIndex = tab) {
                    Tab(tab == 0, { tab = 0 }, text = { Text("Media") })
                    Tab(tab == 1, { tab = 1 }, text = { Text("Docs") })
                    Tab(tab == 2, { tab = 2 }, text = { Text("Links") })
                }
                if (st != null && (!st.filesDone && tab < 2 || st.linksLoading && tab == 2)) {
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                }
            }
        },
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding)) {
            if (st == null) return@Box
            when (tab) {
                0 -> MediaGrid(model, st.files.filter { it.attachment.isMedia }, st.filesDone)
                1 -> DocsList(model, st.files.filter { !it.attachment.isMedia }, st.filesDone)
                else -> {
                    LaunchedEffect(Unit) { model.loadLinks() }
                    LinksList(st.links, st.linksLoading)
                }
            }
        }
    }
}

private val MONTH = DateTimeFormatter.ofPattern("MMMM yyyy")

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun MediaGrid(model: AppModel, files: List<RoomFile>, done: Boolean) {
    if (files.isEmpty()) {
        if (done) EmptyState(Icons.Default.PhotoLibrary, "No photos or videos", "Photos and videos posted in this room show up here.")
        return
    }
    // The viewer swipes through them oldest to newest, as in the chat.
    val gallery = remember(files) { files.asReversed().map { GalleryItem(it.attachment, it.author, it.createdAt) } }
    val months = remember(files) { files.groupBy { localDate(it.createdAt).withDayOfMonth(1) } }
    val haptic = LocalHapticFeedback.current
    LazyVerticalGrid(
        GridCells.Adaptive(104.dp),
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(2.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        for ((month, list) in months) {
            item(key = "m$month", span = { GridItemSpan(maxLineSpan) }) {
                Text(
                    month.format(MONTH),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.padding(start = 12.dp, top = 14.dp, bottom = 6.dp),
                )
            }
            items(list, key = { "${it.seq}/${it.attachment.id}" }) { f ->
                var menu by remember { mutableStateOf(false) }
                val a = f.attachment
                Box(Modifier.aspectRatio(1f)) {
                    MediaThumb(
                        Pic(model.fileUrl(a.id), a.width, a.height, a.name, a.isVideo, a.duration),
                        Modifier.fillMaxSize().combinedClickable(
                            onClick = {
                                val at = gallery.indexOfFirst { it.attachment == a && it.time == f.createdAt }
                                model.showImages(gallery, at.coerceAtLeast(0))
                            },
                            onLongClick = { haptic.performHapticFeedback(HapticFeedbackType.LongPress); menu = true },
                        ),
                        small = true,
                    )
                    FileMenu(model, a, menu) { menu = false }
                }
            }
        }
    }
}

/** Share, and save to the gallery (asking for storage permission before Android 10). */
@Composable
private fun FileMenu(model: AppModel, a: Attachment, shown: Boolean, onDismiss: () -> Unit) {
    val askWrite = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (ok) model.saveFile(a) else model.notice("Can't save without storage permission")
    }
    DropdownMenu(shown, onDismiss) {
        DropdownMenuItem({ Text("Share") }, { onDismiss(); model.shareFile(a) })
        if (a.isMedia) DropdownMenuItem({ Text("Save to gallery") }, {
            onDismiss()
            if (Build.VERSION.SDK_INT < 29) askWrite.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE) else model.saveFile(a)
        })
        if (!a.isMedia) DropdownMenuItem({ Text("Open") }, { onDismiss(); model.openFile(a) })
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun DocsList(model: AppModel, files: List<RoomFile>, done: Boolean) {
    if (files.isEmpty()) {
        if (done) EmptyState(Icons.Default.Description, "No documents", "PDFs posted in this room show up here.")
        return
    }
    val clock = rememberClock()
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(vertical = 6.dp)) {
        items(files, key = { "${it.seq}/${it.attachment.id}" }) { f ->
            var menu by remember { mutableStateOf(false) }
            val a = f.attachment
            Box {
                Row(
                    Modifier.fillMaxWidth()
                        .combinedClickable(onClick = { model.openFile(a) }, onLongClick = { menu = true })
                        .padding(horizontal = 16.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(
                        Modifier.size(44.dp).clip(RoundedCornerShape(10.dp)).background(Color(0xFFE5483A)),
                        contentAlignment = Alignment.Center,
                    ) {
                        if (model.fetching == a.id) CircularProgressIndicator(Modifier.size(22.dp), color = Color.White, strokeWidth = 2.5.dp)
                        else Icon(Icons.Default.PictureAsPdf, null, tint = Color.White)
                    }
                    Spacer(Modifier.width(14.dp))
                    Column(Modifier.weight(1f)) {
                        Text(a.name, style = MaterialTheme.typography.bodyLarge, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Text(
                            "${sizeLabel(a.size)} · ${dayLabel(localDate(f.createdAt))} ${clock(f.createdAt)} · ${f.author.ifBlank { "server" }}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
                FileMenu(model, a, menu) { menu = false }
            }
        }
    }
}

@Composable
private fun LinksList(links: List<dev.vallhala.todo.LinkItem>?, loading: Boolean) {
    val uri = LocalUriHandler.current
    val clock = rememberClock()
    if (links.isNullOrEmpty()) {
        if (!loading && links != null) EmptyState(Icons.Default.Link, "No links", "Links posted in this room show up here.")
        return
    }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(vertical = 6.dp)) {
        items(links, key = { "${it.messageId}/${it.createdAt}/${it.url}" }) { l ->
            Row(
                Modifier.fillMaxWidth().clickable { runCatching { uri.openUri(l.url) } }
                    .padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    Modifier.size(44.dp).clip(RoundedCornerShape(10.dp)).background(MaterialTheme.colorScheme.secondaryContainer),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(Icons.Default.Link, null, tint = MaterialTheme.colorScheme.onSecondaryContainer)
                }
                Spacer(Modifier.width(14.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        l.url.removePrefix("https://").removePrefix("http://"),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.primary,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        "${dayLabel(localDate(l.createdAt))} ${clock(l.createdAt)} · ${l.author.ifBlank { "server" }}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}
