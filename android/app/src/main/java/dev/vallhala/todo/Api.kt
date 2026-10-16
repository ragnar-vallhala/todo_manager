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
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import org.json.JSONArray
import org.json.JSONObject
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
)

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
    val lastMessageId: Long,
)

data class Message(
    val id: Long,
    val roomId: Int,
    val author: String,
    val text: String,
    val createdAt: Long,
    val isEvent: Boolean,
    val todoId: Int?,
)

data class ServerTheme(val name: String, val colors: Map<String, String>)

class ApiException(val status: Int, message: String) : IOException(message)

// ── JSON ──

private fun JSONObject.str(key: String): String? = if (isNull(key)) null else optString(key)
private fun JSONObject.int(key: String): Int? = if (isNull(key)) null else optInt(key)
private fun JSONObject.long(key: String): Long? = if (isNull(key)) null else optLong(key)
private inline fun <T> JSONArray.map(f: (JSONObject) -> T) = List(length()) { f(getJSONObject(it)) }

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
    lastMessageId = o.optLong("lastMessageId"),
)

private fun messageOf(o: JSONObject) = Message(
    id = o.getLong("id"),
    roomId = o.optInt("roomId"),
    author = o.optString("author"),
    text = o.optString("text"),
    createdAt = o.optLong("createdAt"),
    isEvent = o.optString("kind") == "event",
    todoId = o.int("todoId"),
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

    private fun url(path: String, query: Map<String, String> = emptyMap()): HttpUrl {
        val b = base.newBuilder()
        path.trim('/').split('/').forEach { b.addPathSegment(it) }
        query.forEach { (k, v) -> b.addQueryParameter(k, v) }
        // Headers must be ASCII, so the name always goes in the query too.
        if (agent.isNotEmpty()) b.addQueryParameter("agent", agent)
        return b.build()
    }

    private suspend fun call(
        method: String,
        path: String,
        json: JSONObject? = null,
        query: Map<String, String> = emptyMap(),
        client: OkHttpClient = http,
    ): String = withContext(Dispatchers.IO) {
        val rb = Request.Builder().url(url(path, query))
        if (agent.isNotEmpty() && agent.all { it.code in 0x20..0x7e }) rb.header("X-Todo-Agent", agent)
        val payload = json?.toString()?.toRequestBody(JSON)
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
    private suspend fun arr(path: String, query: Map<String, String> = emptyMap(), client: OkHttpClient = http) =
        JSONArray(call("GET", path, query = query, client = client))

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
    suspend fun post(roomId: Int, text: String) = messageOf(obj("POST", "api/rooms/$roomId/messages", body("text" to text, "author" to agent)))

    suspend fun messages(roomId: Int, limit: Int = 200) =
        arr("api/rooms/$roomId/messages", mapOf("limit" to "$limit")).map(::messageOf)

    /** Answers as soon as there's a message after [since], or with nothing after [wait] seconds. */
    suspend fun waitMessages(roomId: Int, since: Long, wait: Int = 50) =
        arr("api/rooms/$roomId/messages", mapOf("since" to "$since", "wait" to "$wait"), longPoll).map(::messageOf)

    companion object {
        private val JSON = "application/json".toMediaType()
        private val http = OkHttpClient.Builder()
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
            .build()
        private val longPoll = http.newBuilder().readTimeout(90, TimeUnit.SECONDS).build()
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
