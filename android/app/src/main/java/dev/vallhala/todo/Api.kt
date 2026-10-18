package dev.vallhala.todo

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import okhttp3.Call
import okhttp3.Callback
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import org.json.JSONArray
import org.json.JSONObject
import okio.BufferedSink
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

// ── Data, as the server sends it ──

data class Todo(
    val id: Int,
    val title: String,
    val done: Boolean,
    val createdAt: Long,
    val categoryId: Int?,
    val roomId: Int?,
    val claimedBy: String?,
    val priority: String?,
    val deadline: String?,
    val description: String?,
    val prunedAt: Long? = null,
    val attachments: List<Attachment> = emptyList(),
)

/** An uploaded file on a message or todo; [width] and [height] are 0 when unknown. */
data class Attachment(
    val id: String,
    val name: String,
    val type: String,
    val size: Long,
    val width: Int = 0,
    val height: Int = 0,
    /** Seconds, for videos whose headers say; else 0. */
    val duration: Double = 0.0,
) {
    val isImage get() = type.startsWith("image/")
    val isVideo get() = type.startsWith("video/")
    /** Shown as a picture: images, and videos by a frame. */
    val isMedia get() = isImage || isVideo
    val isPdf get() = type == "application/pdf"
}

/** An attachment in a room's log, with the message it's on. */
data class RoomFile(val attachment: Attachment, val messageId: Long, val seq: Long, val author: String, val createdAt: Long)

/** One Claude session in a device's usage report. */
data class UsageSession(val name: String, val project: String, val idx30m: Double, val idxWindow: Double)

/** The latest usage report of one device; percentages and reset times are null when unknown. */
data class UsageDevice(
    val device: String,
    val fiveHourPct: Double?,
    val fiveHourResetsAt: Long?,
    val sevenDayPct: Double?,
    val sevenDayResetsAt: Long?,
    val received: Long,
    val sessions: List<UsageSession>,
)

data class Usage(val now: Long, val devices: List<UsageDevice>)

data class CatStyle(
    val fg: String? = null,
    val bg: String? = null,
    val bold: Boolean = false,
    val italic: Boolean = false,
    val underline: Boolean = false,
) {
    val isEmpty get() = fg == null && bg == null && !bold && !italic && !underline
}

data class Category(
    val id: Int,
    val name: String,
    val parentId: Int?,
    val archived: Boolean,
    val style: CatStyle?,
)

data class Member(val name: String, val lastSeen: Long)

data class Room(
    val id: Int,
    val name: String,
    val topic: String,
    val members: List<Member>,
    /** The newest message's seq, the cursor to read or wait after. */
    val cursor: Long,
    val prunedMessages: Int,
    /** Goes up when messages are pruned or restored, so a loaded log is stale. */
    val logVersion: Long,
)

/** [id] is the lowest free in its room (pN once pruned); [seq] only grows, so it orders the log. */
data class Message(
    val id: Long,
    val seq: Long,
    val roomId: Int,
    val author: String,
    val text: String,
    val createdAt: Long,
    val isEvent: Boolean,
    val todoId: Int?,
    val prunedAt: Long? = null,
    val attachments: List<Attachment> = emptyList(),
)

data class ServerTheme(val name: String, val colors: Map<String, String>)

class ApiException(val status: Int, message: String) : IOException(message)

// ── JSON ──

private fun JSONObject.str(key: String): String? = if (isNull(key)) null else optString(key)
private fun JSONObject.int(key: String): Int? = if (isNull(key)) null else optInt(key)
private fun JSONObject.long(key: String): Long? = if (isNull(key)) null else optLong(key)
private inline fun <T> JSONArray.map(f: (JSONObject) -> T) = List(length()) { f(getJSONObject(it)) }

private fun attachmentOf(o: JSONObject) = Attachment(
    id = o.optString("id"),
    name = o.optString("name"),
    type = o.optString("type"),
    size = o.optLong("size"),
    width = o.optInt("width"),
    height = o.optInt("height"),
    duration = o.optDouble("duration", 0.0).takeIf { !it.isNaN() } ?: 0.0,
)

private fun attachmentsOf(o: JSONObject) = o.optJSONArray("attachments")?.map(::attachmentOf) ?: emptyList()

/** What the server wants when attaching uploaded files: their ids and names. */
fun attachmentsJson(list: List<Attachment>) = JSONArray().apply {
    for (a in list) put(JSONObject().put("id", a.id).put("name", a.name))
}

private fun todoOf(o: JSONObject) = Todo(
    id = o.getInt("id"),
    title = o.optString("title"),
    done = o.optBoolean("done"),
    createdAt = o.optLong("createdAt"),
    categoryId = o.int("categoryId"),
    roomId = o.int("roomId"),
    claimedBy = o.str("claimedBy"),
    priority = o.str("priority"),
    deadline = o.str("deadline"),
    description = o.str("description"),
    prunedAt = o.long("prunedAt"),
    attachments = attachmentsOf(o),
)

private fun styleOf(o: JSONObject?) = o?.let {
    CatStyle(it.str("fg"), it.str("bg"), it.optBoolean("bold"), it.optBoolean("italic"), it.optBoolean("underline"))
}

private fun categoryOf(o: JSONObject) = Category(
    id = o.getInt("id"),
    name = o.optString("name"),
    parentId = o.int("parentId"),
    archived = o.optBoolean("archived"),
    style = styleOf(o.optJSONObject("style")),
)

private fun roomOf(o: JSONObject) = Room(
    id = o.getInt("id"),
    name = o.optString("name"),
    topic = o.str("topic") ?: "",
    members = o.optJSONArray("members")?.map { Member(it.optString("name"), it.optLong("lastSeen")) } ?: emptyList(),
    cursor = o.optLong("cursor"),
    prunedMessages = o.optInt("prunedMessages"),
    logVersion = o.optLong("logVersion"),
)

private fun messageOf(o: JSONObject) = Message(
    id = o.getLong("id"),
    seq = o.optLong("seq", o.getLong("id")),
    roomId = o.optInt("roomId"),
    author = o.str("author").orEmpty(),
    text = o.optString("text"),
    createdAt = o.optLong("createdAt"),
    isEvent = o.optString("kind") == "event",
    todoId = o.int("todoId"),
    prunedAt = o.long("prunedAt"),
    attachments = attachmentsOf(o),
)

private fun themeOf(o: JSONObject): ServerTheme {
    val c = o.optJSONObject("colors") ?: JSONObject()
    return ServerTheme(o.optString("name"), c.keys().asSequence().associateWith { c.optString(it) })
}

fun styleJson(s: CatStyle?): Any = if (s == null || s.isEmpty) JSONObject.NULL else JSONObject().apply {
    s.fg?.let { put("fg", it) }
    s.bg?.let { put("bg", it) }
    if (s.bold) put("bold", true)
    if (s.italic) put("italic", true)
    if (s.underline) put("underline", true)
}

/** A JSON body; null values are sent as JSON null (e.g. to clear a deadline). */
fun body(vararg pairs: Pair<String, Any?>) = JSONObject().apply {
    for ((k, v) in pairs) put(k, v ?: JSONObject.NULL)
}

// ── Client ──

/**
 * The todo server's REST API. [agent] is who we are in rooms: it's sent with
 * every request so room events and member lists name us.
 */
class Api(baseUrl: String, private val agent: String) {
    private val base: HttpUrl = baseUrl.trim().trimEnd('/').let { if ("://" in it) it else "http://$it" }
        .toHttpUrlOrNull() ?: throw IllegalArgumentException("Not a valid server address: $baseUrl")

    private fun url(path: String, query: Map<String, String> = emptyMap(), anonymous: Boolean = false): HttpUrl {
        val b = base.newBuilder()
        path.trim('/').split('/').forEach { b.addPathSegment(it) }
        query.forEach { (k, v) -> b.addQueryParameter(k, v) }
        // Headers must be ASCII, so the name always goes in the query too.
        if (agent.isNotEmpty() && !anonymous) b.addQueryParameter("agent", agent)
        return b.build()
    }

    /** Where an uploaded file can be fetched (no agent: it's the same for everyone, so caches share it). */
    fun fileUrl(id: String): String = base.newBuilder().addPathSegments("api/files").addPathSegment(id).build().toString()

    private suspend fun call(
        method: String,
        path: String,
        json: JSONObject? = null,
        query: Map<String, String> = emptyMap(),
        client: OkHttpClient = http,
        anonymous: Boolean = false,
        raw: RequestBody? = null,
    ): String = withContext(Dispatchers.IO) {
        val rb = Request.Builder().url(url(path, query, anonymous))
        if (!anonymous && agent.isNotEmpty() && agent.all { it.code in 0x20..0x7e }) rb.header("X-Todo-Agent", agent)
        val payload = raw ?: json?.toString()?.toRequestBody(JSON)
        rb.method(method, payload ?: if (method == "GET" || method == "DELETE") null else "{}".toRequestBody(JSON))
        client.newCall(rb.build()).await().use { res ->
            val text = res.body?.string().orEmpty()
            if (!res.isSuccessful) {
                val msg = runCatching { JSONObject(text).optString("error") }.getOrNull()
                throw ApiException(res.code, msg?.takeIf { it.isNotEmpty() } ?: "Server said ${res.code}")
            }
            text
        }
    }

    private suspend fun obj(method: String, path: String, json: JSONObject? = null) = JSONObject(call(method, path, json))
    private suspend fun arr(path: String, query: Map<String, String> = emptyMap(), client: OkHttpClient = http, anonymous: Boolean = false) =
        JSONArray(call("GET", path, query = query, client = client, anonymous = anonymous))

    // Usage
    suspend fun usage(): Usage {
        val o = JSONObject(call("GET", "api/usage"))
        fun JSONObject.dbl(k: String) = if (has(k) && !isNull(k)) optDouble(k) else null
        return Usage(o.optLong("now"), (o.optJSONArray("devices") ?: JSONArray()).map { d ->
            UsageDevice(
                d.optString("device"), d.dbl("five_hour_pct"), d.dbl("five_hour_resets_at")?.toLong(),
                d.dbl("seven_day_pct"), d.dbl("seven_day_resets_at")?.toLong(), d.optLong("received"),
                (d.optJSONArray("sessions") ?: JSONArray()).map { s ->
                    UsageSession(s.optString("name"), s.optString("project"), s.optDouble("idx_30m", 0.0), s.optDouble("idx_window", 0.0))
                },
            )
        })
    }

    // Todos
    suspend fun todos() = arr("api/todos", mapOf("status" to "all", "archived" to "1")).map(::todoOf)
    suspend fun addTodo(json: JSONObject) = todoOf(obj("POST", "api/todos", json))
    suspend fun updateTodo(id: Int, json: JSONObject) = todoOf(obj("PATCH", "api/todos/$id", json))
    suspend fun deleteTodo(id: Int) { call("DELETE", "api/todos/$id") }
    suspend fun pruneTodo(id: Int) { call("POST", "api/todos/$id/prune") }
    suspend fun pruneDone(): Int = obj("POST", "api/todos/prune").optInt("pruned")
    suspend fun claim(id: Int, force: Boolean) = todoOf(obj("POST", "api/todos/$id/claim", body("agent" to agent, "force" to force)))
    suspend fun release(id: Int, force: Boolean) = todoOf(obj("POST", "api/todos/$id/release", body("agent" to agent, "force" to force)))

    // Pruned
    suspend fun pruned() = arr("api/pruned").map(::todoOf)
    suspend fun restore(id: Int) = todoOf(obj("POST", "api/pruned/$id/restore"))
    suspend fun deletePruned(id: Int) { call("DELETE", "api/pruned/$id") }
    suspend fun deleteAllPruned() { call("DELETE", "api/pruned") }

    // Categories
    suspend fun categories() = arr("api/categories", mapOf("archived" to "1")).map(::categoryOf)
    suspend fun addCategory(name: String, parentId: Int?) = categoryOf(obj("POST", "api/categories", body("name" to name, "parentId" to parentId)))
    suspend fun updateCategory(id: Int, json: JSONObject) = categoryOf(obj("PATCH", "api/categories/$id", json))
    suspend fun deleteCategory(id: Int) { call("DELETE", "api/categories/$id") }

    // Themes
    suspend fun themes() = arr("api/themes").map(::themeOf)

    // Rooms
    suspend fun rooms() = arr("api/rooms").map(::roomOf)
    suspend fun addRoom(name: String, topic: String) = roomOf(obj("POST", "api/rooms", body("name" to name, "topic" to topic)))
    suspend fun updateRoom(id: Int, json: JSONObject) = roomOf(obj("PATCH", "api/rooms/$id", json))
    suspend fun deleteRoom(id: Int) { call("DELETE", "api/rooms/$id") }
    suspend fun post(roomId: Int, text: String, attachments: List<Attachment> = emptyList()): Message {
        val json = body("text" to text, "author" to agent)
        if (attachments.isNotEmpty()) json.put("attachments", attachmentsJson(attachments))
        return messageOf(obj("POST", "api/rooms/$roomId/messages", json))
    }

    /** The newest [limit] messages after [since] (and before seq [before], if set), oldest first. */
    suspend fun messages(roomId: Int, limit: Int = 100, since: Long = 0, before: Long = 0) =
        arr(
            "api/rooms/$roomId/messages",
            mapOf("limit" to "$limit", "since" to "$since") + if (before > 0) mapOf("before" to "$before") else emptyMap(),
        ).map(::messageOf)

    /** Room [roomId]'s attachments, newest first, from messages before seq [before] (0: the newest). */
    suspend fun roomFiles(roomId: Int, before: Long = 0, limit: Int = 500) =
        arr(
            "api/rooms/$roomId/files",
            mapOf("limit" to "$limit") + if (before > 0) mapOf("before" to "$before") else emptyMap(),
            anonymous = true,
        ).map { RoomFile(attachmentOf(it), it.optLong("messageId"), it.optLong("seq"), it.str("author").orEmpty(), it.optLong("createdAt")) }

    /** Like [messages], but without saying who we are, so peeking at a room (for the room list) doesn't join it. */
    suspend fun peek(roomId: Int, limit: Int, since: Long = 0) =
        arr("api/rooms/$roomId/messages", mapOf("limit" to "$limit", "since" to "$since"), anonymous = true).map(::messageOf)

    // Files

    /** Uploads [file] as [name]; [progress] gets the fraction sent so far. */
    suspend fun upload(file: File, name: String, progress: (Float) -> Unit = {}): Attachment {
        val raw = ProgressBody(file, progress)
        return attachmentOf(JSONObject(call("POST", "api/files", query = mapOf("name" to name), client = uploads, raw = raw)))
    }

    /** Downloads file [id] to [dest] (via a temporary file, so a half-done one is never left there). */
    suspend fun download(id: String, dest: File) = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(fileUrl(id)).build()
        http.newCall(req).await().use { res ->
            if (!res.isSuccessful) throw ApiException(res.code, "Couldn't fetch the file (${res.code})")
            dest.parentFile?.mkdirs()
            val tmp = File(dest.path + ".part")
            tmp.outputStream().use { out -> res.body!!.byteStream().copyTo(out) }
            if (!tmp.renameTo(dest)) throw IOException("Couldn't save the file")
        }
        dest
    }

    // Pruned messages
    suspend fun pruneMessage(roomId: Int, id: Long) { call("POST", "api/rooms/$roomId/messages/$id/prune") }
    suspend fun pruneMessages(roomId: Int, keep: Int = 0): Int =
        obj("POST", "api/rooms/$roomId/messages/prune", body("keep" to keep)).optInt("pruned")
    suspend fun prunedMessages(roomId: Int) = arr("api/rooms/$roomId/pruned").map(::messageOf)
    suspend fun restoreMessage(roomId: Int, id: Long) = messageOf(obj("POST", "api/rooms/$roomId/pruned/$id/restore"))
    suspend fun deletePrunedMessage(roomId: Int, id: Long) { call("DELETE", "api/rooms/$roomId/pruned/$id") }
    suspend fun deleteAllPrunedMessages(roomId: Int) { call("DELETE", "api/rooms/$roomId/pruned") }

    /** Answers as soon as there's a message with seq after [since], or with nothing after [wait] seconds. */
    suspend fun waitMessages(roomId: Int, since: Long, wait: Int = 50) =
        arr("api/rooms/$roomId/messages", mapOf("since" to "$since", "wait" to "$wait"), longPoll).map(::messageOf)

    companion object {
        private val JSON = "application/json".toMediaType()
        /** Shared with the image loader, so they use one connection pool. */
        val http: OkHttpClient = OkHttpClient.Builder()
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
            .build()
        private val longPoll = http.newBuilder().readTimeout(90, TimeUnit.SECONDS).build()
        private val uploads = http.newBuilder().writeTimeout(120, TimeUnit.SECONDS).readTimeout(60, TimeUnit.SECONDS).build()
    }
}

/** A file as a request body, reporting how much of it has been written. */
private class ProgressBody(private val file: File, private val progress: (Float) -> Unit) : RequestBody() {
    override fun contentType() = "application/octet-stream".toMediaType()
    override fun contentLength() = file.length()
    override fun writeTo(sink: BufferedSink) {
        val total = file.length().coerceAtLeast(1)
        var sent = 0L
        file.inputStream().use { input ->
            val buf = ByteArray(32 * 1024)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                sink.write(buf, 0, n)
                sent += n
                progress(sent.toFloat() / total)
            }
        }
    }
}

/** Runs the call, cancelling it if the coroutine is cancelled. */
private suspend fun Call.await(): Response = suspendCancellableCoroutine { cont ->
    cont.invokeOnCancellation { cancel() }
    enqueue(object : Callback {
        override fun onResponse(call: Call, response: Response) = cont.resume(response)
        override fun onFailure(call: Call, e: IOException) {
            if (!cont.isCancelled) cont.resumeWithException(e)
        }
    })
}
