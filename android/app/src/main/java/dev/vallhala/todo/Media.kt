package dev.vallhala.todo

import android.content.ActivityNotFoundException
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.graphics.Matrix
import android.media.ExifInterface
import android.media.MediaMetadataRetriever
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.provider.OpenableColumns
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.IOException
import java.util.UUID
import kotlin.math.max

/** A file ready to upload: copied (and for big photos, shrunk) into our cache. */
data class Draft(
    val file: File,
    val name: String,
    val type: String,
    val size: Long,
    val width: Int = 0,
    val height: Int = 0,
    val duration: Double = 0.0,
) {
    val isImage get() = type.startsWith("image/")
    val isVideo get() = type.startsWith("video/")
    val isMedia get() = isImage || isVideo
}

/** Something the user should be told, such as a file too big to send. */
class MediaException(message: String) : Exception(message)

object Media {
    /** The server's limit per file: images and PDFs, then videos. */
    const val MAX_SIZE = 10L * 1024 * 1024
    const val MAX_VIDEO_SIZE = 50L * 1024 * 1024
    /** Photos bigger than this, or from the camera, are shrunk and sent as JPEG. */
    private const val RECOMPRESS_OVER = 2L * 1024 * 1024
    /** The long edge of a shrunk photo. */
    private const val MAX_EDGE = 2048

    fun authority(ctx: Context) = "${ctx.packageName}.files"

    private fun dir(ctx: Context, name: String) = File(ctx.cacheDir, name).apply { mkdirs() }

    /** A fresh file for the camera to write a photo (or a [video]) to, and its content uri. */
    fun cameraTarget(ctx: Context, video: Boolean = false): Pair<File, Uri> {
        val f = File(dir(ctx, "camera"), if (video) "VID_${System.currentTimeMillis()}.mp4" else "IMG_${System.currentTimeMillis()}.jpg")
        return f to FileProvider.getUriForFile(ctx, authority(ctx), f)
    }

    /** What the first bytes say the file is: one of the types the server takes, or null. */
    fun sniff(head: ByteArray): String? {
        fun at(i: Int, vararg b: Int) = head.size >= i + b.size && b.indices.all { head[i + it].toInt() and 0xff == b[it] }
        return when {
            at(0, 0x89, 0x50, 0x4E, 0x47) -> "image/png"
            at(0, 0xFF, 0xD8, 0xFF) -> "image/jpeg"
            at(0, 0x47, 0x49, 0x46, 0x38) -> "image/gif"
            at(0, 0x52, 0x49, 0x46, 0x46) && at(8, 0x57, 0x45, 0x42, 0x50) -> "image/webp"
            at(0, 0x25, 0x50, 0x44, 0x46) -> "application/pdf"
            // MP4: an ftyp box, unless it's an image's (HEIF, AVIF) or QuickTime, as the server judges.
            at(4, 0x66, 0x74, 0x79, 0x70) && head.size >= 12 ->
                String(head, 8, 4, Charsets.ISO_8859_1).takeIf { it !in NOT_MP4 }?.let { "video/mp4" }
            at(0, 0x1A, 0x45, 0xDF, 0xA3) && String(head, Charsets.ISO_8859_1).contains("webm") -> "video/webm"
            else -> null
        }
    }

    private val NOT_MP4 = setOf("heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1", "avif", "avis", "qt  ", "crx ")

    private fun declaredSize(ctx: Context, uri: Uri): Long? = runCatching {
        ctx.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c ->
            if (c.moveToFirst() && !c.isNull(0)) c.getLong(0) else null
        }
    }.getOrNull()

    private fun displayName(ctx: Context, uri: Uri): String? = runCatching {
        ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull() ?: uri.lastPathSegment?.substringAfterLast('/')

    /**
     * Copies [uri] into the cache as something the server will take: PDFs and
     * small images as they are, big or camera photos (and image types the
     * server doesn't know, like HEIC) shrunk to a JPEG under the limit.
     */
    suspend fun prepare(ctx: Context, uri: Uri, fromCamera: Boolean = false): Draft = withContext(Dispatchers.IO) {
        val cr = ctx.contentResolver
        val name = displayName(ctx, uri)?.takeIf { it.isNotBlank() } ?: "file"
        val declared = declaredSize(ctx, uri)
        // Nothing the server takes is over 50 MB: don't copy a huge file just to refuse it.
        if (declared != null && declared > MAX_VIDEO_SIZE) throw MediaException("$name is over 50 MB")
        val out = File(dir(ctx, "outgoing"), UUID.randomUUID().toString())
        cr.openInputStream(uri)?.use { input ->
            out.outputStream().use { o ->
                val buf = ByteArray(64 * 1024)
                var total = 0L
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    total += n
                    if (total > MAX_VIDEO_SIZE) { o.close(); out.delete(); throw MediaException("$name is over 50 MB") }
                    o.write(buf, 0, n)
                }
            }
        } ?: throw MediaException("Couldn't read $name")
        val head = out.inputStream().use { s -> ByteArray(64).also { s.read(it) } }
        val sniffed = sniff(head)
        val mime = cr.getType(uri).orEmpty()
        val size = out.length()
        when {
            sniffed == "application/pdf" -> {
                if (size > MAX_SIZE) { out.delete(); throw MediaException("$name is over 10 MB") }
                Draft(out, name, sniffed, size)
            }
            sniffed == "video/mp4" || sniffed == "video/webm" -> {
                // With its extension, so the image loader knows to take a frame from it for the thumbnail.
                val named = File(out.path + if (sniffed == "video/mp4") ".mp4" else ".webm")
                val file = if (out.renameTo(named)) named else out
                val (w, h, secs) = videoInfo(file)
                Draft(file, name, sniffed, size, w, h, secs)
            }
            mime.startsWith("video/") -> {
                out.delete()
                throw MediaException("Only MP4 and WebM videos can be sent")
            }
            sniffed == "image/gif" -> {
                if (size > MAX_SIZE) { out.delete(); throw MediaException("$name is over 10 MB") }
                bounds(out).let { (w, h) -> Draft(out, name, sniffed, size, w, h) }
            }
            sniffed != null || mime.startsWith("image/") -> {
                val rotated = sniffed == "image/jpeg" && orientation(out) != ExifInterface.ORIENTATION_NORMAL
                if (sniffed != null && !fromCamera && !rotated && size <= RECOMPRESS_OVER) {
                    bounds(out).let { (w, h) -> Draft(out, name, sniffed, size, w, h) }
                } else {
                    val jpeg = File(out.path + ".jpg")
                    try {
                        shrink(out, jpeg)
                    } finally {
                        out.delete()
                    }
                    val (w, h) = bounds(jpeg)
                    Draft(jpeg, name.substringBeforeLast('.').ifBlank { "photo" } + ".jpg", "image/jpeg", jpeg.length(), w, h)
                }
            }
            else -> {
                out.delete()
                throw MediaException("Only images, videos and PDFs can be sent")
            }
        }
    }

    /** A video's size as shown (turned for portrait clips) and its length in seconds; zeros where unknown. */
    private fun videoInfo(f: File): Triple<Int, Int, Double> {
        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(f.path)
            fun num(key: Int) = r.extractMetadata(key)?.toLongOrNull() ?: 0L
            val w = num(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH).toInt()
            val h = num(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT).toInt()
            val turned = num(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION) % 180 == 90L
            Triple(if (turned) h else w, if (turned) w else h, num(MediaMetadataRetriever.METADATA_KEY_DURATION) / 1000.0)
        } catch (e: Exception) {
            Triple(0, 0, 0.0)
        } finally {
            runCatching { r.release() }
        }
    }

    private fun bounds(f: File): Pair<Int, Int> {
        val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(f.path, o)
        return o.outWidth.coerceAtLeast(0) to o.outHeight.coerceAtLeast(0)
    }

    private fun orientation(f: File) = runCatching {
        ExifInterface(f.path).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
    }.getOrDefault(ExifInterface.ORIENTATION_NORMAL)

    /** Decodes [src] at most [MAX_EDGE] on its long edge, upright, and writes it to [dest] as a JPEG under the limit. */
    private fun shrink(src: File, dest: File) {
        val bmp = decodeUpright(src) ?: throw MediaException("Couldn't read that image")
        try {
            var quality = 85
            while (true) {
                dest.outputStream().use { bmp.compress(Bitmap.CompressFormat.JPEG, quality, it) }
                if (dest.length() <= MAX_SIZE || quality <= 40) break
                quality -= 15
            }
            if (dest.length() > MAX_SIZE) throw MediaException("That image is too big to send")
        } finally {
            bmp.recycle()
        }
    }

    private fun decodeUpright(src: File): Bitmap? {
        if (Build.VERSION.SDK_INT >= 28) {
            // ImageDecoder applies the EXIF rotation itself and reads HEIC too.
            return runCatching {
                ImageDecoder.decodeBitmap(ImageDecoder.createSource(src)) { d, info, _ ->
                    val s = info.size
                    val scale = MAX_EDGE.toFloat() / max(s.width, s.height)
                    if (scale < 1f) d.setTargetSize((s.width * scale).toInt().coerceAtLeast(1), (s.height * scale).toInt().coerceAtLeast(1))
                    d.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                }
            }.getOrNull()
        }
        val (w, h) = bounds(src)
        if (w <= 0 || h <= 0) return null
        var sample = 1
        while (max(w, h) / (sample * 2) >= MAX_EDGE) sample *= 2
        val raw = BitmapFactory.decodeFile(src.path, BitmapFactory.Options().apply { inSampleSize = sample }) ?: return null
        val m = Matrix()
        val edge = max(raw.width, raw.height)
        if (edge > MAX_EDGE) (MAX_EDGE.toFloat() / edge).let { m.postScale(it, it) }
        when (orientation(src)) {
            ExifInterface.ORIENTATION_ROTATE_90 -> m.postRotate(90f)
            ExifInterface.ORIENTATION_ROTATE_180 -> m.postRotate(180f)
            ExifInterface.ORIENTATION_ROTATE_270 -> m.postRotate(270f)
            ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> m.postScale(-1f, 1f)
            ExifInterface.ORIENTATION_FLIP_VERTICAL -> m.postScale(1f, -1f)
        }
        if (m.isIdentity) return raw
        return Bitmap.createBitmap(raw, 0, 0, raw.width, raw.height, m, true).also { if (it !== raw) raw.recycle() }
    }

    /** A name safe to put in a file path. */
    private fun safeName(a: Attachment): String {
        val ext = when (a.type) {
            "image/png" -> "png"; "image/jpeg" -> "jpg"; "image/gif" -> "gif"; "image/webp" -> "webp"
            "application/pdf" -> "pdf"; "video/mp4" -> "mp4"; "video/webm" -> "webm"; else -> "bin"
        }
        val base = a.name.replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), "_").trim().take(120)
        return when {
            base.isEmpty() -> "file.$ext"
            base.contains('.') -> base
            else -> "$base.$ext"
        }
    }

    /** Attachment [a] as a local file, downloaded once (its id is its content's hash, so it never changes). */
    suspend fun local(ctx: Context, api: Api, a: Attachment): File {
        val f = File(File(dir(ctx, "files"), a.id), safeName(a))
        return if (f.exists()) f else api.download(a.id, f)
    }

    private fun uriOf(ctx: Context, f: File) = FileProvider.getUriForFile(ctx, authority(ctx), f)

    /** Opens a file with whatever app views its type (the system's PDF viewer, say). */
    fun open(ctx: Context, f: File, type: String) {
        val i = Intent(Intent.ACTION_VIEW).setDataAndType(uriOf(ctx, f), type)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            ctx.startActivity(i)
        } catch (e: ActivityNotFoundException) {
            throw MediaException("No app here can open ${if (type == "application/pdf") "PDFs" else "this file"}")
        }
    }

    fun share(ctx: Context, f: File, type: String) {
        val send = Intent(Intent.ACTION_SEND).setType(type)
            .putExtra(Intent.EXTRA_STREAM, uriOf(ctx, f))
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        ctx.startActivity(Intent.createChooser(send, null).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    /**
     * Copies an image into Pictures/Todo (a video into Movies/Todo), where
     * gallery apps find it. Before Android 10 this needs storage permission.
     */
    suspend fun saveToGallery(ctx: Context, f: File, type: String) = withContext(Dispatchers.IO) {
        val video = type.startsWith("video/")
        if (Build.VERSION.SDK_INT >= 29) {
            val cr = ctx.contentResolver
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, f.name)
                put(MediaStore.MediaColumns.MIME_TYPE, type)
                put(MediaStore.MediaColumns.RELATIVE_PATH, (if (video) Environment.DIRECTORY_MOVIES else Environment.DIRECTORY_PICTURES) + "/Todo")
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
            val collection = if (video) MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
            else MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
            val uri = cr.insert(collection, values) ?: throw IOException("Couldn't save to the gallery")
            try {
                cr.openOutputStream(uri)?.use { out -> f.inputStream().use { it.copyTo(out) } } ?: throw IOException("Couldn't save to the gallery")
                cr.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
            } catch (e: Exception) {
                cr.delete(uri, null, null)
                throw e
            }
        } else {
            @Suppress("DEPRECATION")
            val dir = File(Environment.getExternalStoragePublicDirectory(if (video) Environment.DIRECTORY_MOVIES else Environment.DIRECTORY_PICTURES), "Todo").apply { mkdirs() }
            var dest = File(dir, f.name)
            var n = 1
            while (dest.exists()) dest = File(dir, "${f.nameWithoutExtension} (${n++}).${f.extension}")
            f.copyTo(dest)
            MediaScannerConnection.scanFile(ctx, arrayOf(dest.path), arrayOf(type), null)
        }
    }

    /** Drops the copies made for sending once they're sent (or abandoned). */
    fun discard(drafts: List<Draft>) {
        for (d in drafts) runCatching { d.file.delete() }
    }
}

/** "0:42", "12:05", "1:02:03". */
fun durationLabel(seconds: Double): String {
    val t = Math.round(seconds).toInt()
    val h = t / 3600
    val m = t % 3600 / 60
    val s = t % 60
    return if (h > 0) String.format(java.util.Locale.US, "%d:%02d:%02d", h, m, s) else String.format(java.util.Locale.US, "%d:%02d", m, s)
}

/** "2.4 MB", "830 KB". */
fun sizeLabel(bytes: Long): String = when {
    bytes >= 1024 * 1024 -> String.format(java.util.Locale.US, "%.1f MB", bytes / 1048576.0)
    bytes >= 1024 -> "${bytes / 1024} KB"
    else -> "$bytes B"
}
