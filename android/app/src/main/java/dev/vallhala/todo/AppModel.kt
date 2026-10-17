package dev.vallhala.todo

import android.app.Application
import androidx.compose.runtime.getValue
import android.net.Uri
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.io.IOException

/** What the main screen shows. */
sealed interface View {
    data object All : View
    data object Inbox : View
    data object Archive : View
    data object Pruned : View
    data class Cat(val id: Int) : View
    data class RoomView(val id: Int) : View

    fun key() = when (this) {
        All -> "all"; Inbox -> "inbox"; Archive -> "archive"; Pruned -> "pruned"
        is Cat -> "c$id"; is RoomView -> "r$id"
    }

    companion object {
        fun parse(s: String): View = when {
            s == "inbox" -> Inbox
            s == "archive" -> Archive
            s == "pruned" -> Pruned
            s.startsWith("c") && s.drop(1).toIntOrNull() != null -> Cat(s.drop(1).toInt())
            s.startsWith("r") && s.drop(1).toIntOrNull() != null -> RoomView(s.drop(1).toInt())
            else -> All
        }
    }
}

/** A group of open todos under a category heading (no heading for the view's own). */
data class Group(val catId: Int?, val title: String?, val todos: List<Todo>)

/** Screens pushed over the home screen (todos and chats), newest last. */
sealed interface Screen {
    data class Chat(val roomId: Int) : Screen
    data class RoomTodos(val roomId: Int) : Screen
    data class RoomPruned(val roomId: Int) : Screen
    /** A room's media, docs and links. */
    data class RoomMedia(val roomId: Int) : Screen
    /** Picking a room for files shared from another app. */
    data object Share : Screen

    val key: String
        get() = when (this) {
            is Chat -> "chat$roomId"; is RoomTodos -> "todos$roomId"; is RoomPruned -> "pruned$roomId"
            is RoomMedia -> "media$roomId"; Share -> "share"
        }
}

/** The room list's line for a room: its newest message and how many are unread. */
data class Preview(val last: Message?, val unread: Int, val mentioned: Boolean)

/** One image in the full-screen viewer, with who sent it and when, if it's from a room. */
data class GalleryItem(val attachment: Attachment, val author: String? = null, val time: Long? = null)

data class Gallery(val items: List<GalleryItem>, val start: Int)

/** A link someone posted in a room. */
data class LinkItem(val url: String, val author: String, val createdAt: Long, val messageId: Long)

/** Everything a room's gallery shows: its files (all of them, loaded page by page) and, once asked for, its links. */
class RoomMediaState(val roomId: Int) {
    var files by mutableStateOf<List<RoomFile>>(emptyList())
    var filesDone by mutableStateOf(false)
    var links by mutableStateOf<List<LinkItem>?>(null)
    var linksLoading by mutableStateOf(false)
}

/** A message on its way: its files uploading, then the post. Shown in the log until the server has it. */
class Outgoing(val key: Long, val roomId: Int, val text: String, val drafts: List<Draft>, val createdAt: Long) {
    var progress by mutableFloatStateOf(0f)
    var failed by mutableStateOf<String?>(null)
}

class AppModel(app: Application) : AndroidViewModel(app) {
    private val ctx get() = getApplication<Application>()
    val prefs = Prefs(app)
    private var api = Api(prefs.server, prefs.agent)

    var agent by mutableStateOf(prefs.agent); private set
    var server by mutableStateOf(prefs.server); private set
    var needsSetup by mutableStateOf(!prefs.configured); private set

    var todos by mutableStateOf<List<Todo>>(emptyList()); private set
    var cats by mutableStateOf<List<Category>>(emptyList()); private set
    var rooms by mutableStateOf<List<Room>>(emptyList()); private set
    var pruned by mutableStateOf<List<Todo>>(emptyList()); private set
    var themes by mutableStateOf<List<ServerTheme>>(emptyList()); private set
    var themeName by mutableStateOf(prefs.themeName); private set
    var themeColors by mutableStateOf(prefs.themeColors); private set

    var view by mutableStateOf(View.parse(prefs.view)); private set
    var showDone by mutableStateOf(prefs.showDone); private set
    var collapsed by mutableStateOf(prefs.collapsed); private set
    var lastRead by mutableStateOf(prefs.lastRead); private set
    var pinned by mutableStateOf(prefs.pinned); private set
    var homeTab by mutableIntStateOf(prefs.homeTab); private set

    var loading by mutableStateOf(false); private set
    var loaded by mutableStateOf(false); private set
    /** Set while the server can't be reached; cleared by the next successful load. */
    var offline by mutableStateOf<String?>(null); private set
    /** One-off messages for the snackbar. */
    val notices = mutableStateListOf<String>()

    /** Screens over the home screen; the last one shows. */
    val stack = mutableStateListOf<Screen>()
    /** The full-screen image viewer, when open. */
    var gallery by mutableStateOf<Gallery?>(null); private set

    /** The room list's previews, by room id. */
    var previews by mutableStateOf<Map<Int, Preview>>(emptyMap()); private set
    /** The seq up to which each room's @mentions have been notified (or were there before we looked). */
    private val notifiedUpTo = HashMap<Int, Long>()

    /** The open room's log, by seq. */
    val log = mutableStateListOf<Message>()
    private var logRoom: Int? = null
    private var logLimit = PAGE
    /** Whether the server may have messages older than the loaded log. */
    var hasOlder by mutableStateOf(false); private set
    var loadingOlder by mutableStateOf(false); private set
    /** Whether the open room's log has been fetched once. */
    var logLoaded by mutableStateOf(false); private set
    /** False while the open room's live updates can't reach the server. */
    var linkUp by mutableStateOf(true); private set
    /** What was read in the open room when it was opened, for the "unread messages" line. */
    var unreadFrom by mutableStateOf<Long?>(null); private set
    /** The open room's pruned messages, once asked for (null until then). */
    var prunedLog by mutableStateOf<List<Message>?>(null); private set
    private var poll: Job? = null
    private var ticker: Job? = null
    private var foreground = false

    /** Messages being sent, in every room. */
    val outgoing = mutableStateListOf<Outgoing>()
    private var nextKey = 1L
    /** Files picked for each room's next message. */
    val drafts = mutableStateMapOf<Int, List<Draft>>()
    /** How many picked files are still being copied in. */
    var preparing by mutableIntStateOf(0); private set
    /** Files shared from another app, waiting for a room. */
    var shared by mutableStateOf<List<Draft>>(emptyList()); private set
    /** The attachment being fetched to open, share or save. */
    var fetching by mutableStateOf<String?>(null); private set
    /** The open room gallery's contents. */
    var roomMedia by mutableStateOf<RoomMediaState?>(null); private set
    /** The todo whose new attachments are uploading. */
    var todoUploading by mutableStateOf<Int?>(null); private set

    val chatRoom: Int? get() = (stack.lastOrNull { it is Screen.Chat } as? Screen.Chat)?.roomId

    init {
        if (!needsSetup) refresh()
    }

    // ── Loading ──

    fun refresh() = launch(quiet = true) {
        loading = true
        try {
            val t = api.todos()
            val c = api.categories()
            val r = api.rooms()
            val p = api.pruned()
            todos = t; cats = c; pruned = p
            applyRooms(r)
            offline = null
            loaded = true
            // A view that no longer exists (deleted elsewhere) falls back to All.
            val v = view
            if ((v is View.Cat && cats.none { it.id == v.id }) || (v is View.RoomView && rooms.none { it.id == v.id })) go(View.All)
            stack.removeAll { s ->
                val id = when (s) {
                    is Screen.Chat -> s.roomId; is Screen.RoomTodos -> s.roomId; is Screen.RoomPruned -> s.roomId
                    is Screen.RoomMedia -> s.roomId; else -> null
                }
                id != null && rooms.none { it.id == id }
            }
            syncPoll()
        } finally {
            loading = false
        }
        runCatching { api.themes() }.getOrNull()?.let { list ->
            themes = list
            // Pick up edits to the chosen theme's file.
            list.find { it.name == themeName }?.let { setTheme(it) }
        }
    }

    /** Takes a fresh room list: reloads the open log if it went stale, and the previews that changed. */
    private suspend fun applyRooms(r: List<Room>) {
        // Messages pruned or restored elsewhere: the loaded log is stale.
        val stale = logRoom?.let { id -> room(id)?.logVersion != r.find { it.id == id }?.logVersion } == true
        val old = rooms.associateBy { it.id }
        // The open room's cursor may be ahead of this list (we saw a message after it was made).
        rooms = r.map { n -> old[n.id]?.takeIf { it.id == logRoom && it.cursor > n.cursor && !stale }?.let { n.copy(cursor = it.cursor) } ?: n }
        if (stale) reloadLog()
        loadPreviews()
    }

    /** Peeks at each room whose cursor moved since its preview: its newest message, and what's unread. */
    private suspend fun loadPreviews() = coroutineScope {
        val list = rooms
        val fresh = list.map { r ->
            async {
                val have = previews[r.id]
                val read = lastRead[r.id] ?: 0
                when {
                    r.id == logRoom -> r.id to Preview(log.lastOrNull() ?: have?.last, 0, false)
                    have != null && (have.last?.seq ?: 0) == r.cursor && (have.unread == 0) == (r.cursor <= read) -> r.id to have
                    r.cursor == 0L -> r.id to Preview(null, 0, false)
                    else -> runCatching {
                        if (r.cursor > read) {
                            val msgs = api.peek(r.id, MAX_UNREAD, since = read)
                            val theirs = msgs.filter { it.author != agent && !it.isEvent }
                            checkMentions(r, theirs)
                            r.id to Preview(msgs.lastOrNull(), theirs.size, theirs.any { mentions(it) })
                        } else {
                            r.id to Preview(api.peek(r.id, 1).lastOrNull(), 0, false)
                        }
                    }.getOrElse { r.id to (have ?: Preview(null, 0, false)) }
                }
            }
        }.awaitAll().toMap()
        previews = fresh
        for (r in list) if (r.id !in notifiedUpTo) notifiedUpTo[r.id] = r.cursor
    }

    fun mentions(m: Message) = !m.isEvent && m.author != agent &&
        Regex("(?<![\\w@])@${Regex.escape(agent)}(?![\\w-])", RegexOption.IGNORE_CASE).containsMatchIn(m.text)

    /** Notifies about messages mentioning us that are new since we last looked (never about the open room). */
    private fun checkMentions(r: Room, msgs: List<Message>) {
        val from = notifiedUpTo[r.id] ?: return  // First look: what's there already isn't news.
        if (r.id != chatRoom || !foreground) {
            msgs.lastOrNull { it.seq > from && mentions(it) }?.let { Notify.mention(ctx, r, it) }
        }
        notifiedUpTo[r.id] = maxOf(from, r.cursor)
    }

    /** Called when the app comes to the front or goes away: rooms are only polled while it's visible. */
    fun setForeground(on: Boolean) {
        foreground = on
        if (on) {
            if (!needsSetup) refresh()
            syncPoll()
            startTicker()
        } else {
            poll?.cancel(); poll = null
            ticker?.cancel(); ticker = null
        }
    }

    /** While we're visible: the room list every few seconds, everything every half minute. */
    private fun startTicker() {
        if (ticker?.isActive == true) return
        ticker = viewModelScope.launch {
            var n = 0
            while (isActive) {
                delay(TICK)
                if (needsSetup) continue
                if (++n % 6 == 0) refresh().join()
                else try {
                    applyRooms(api.rooms())
                    offline = null
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    if (e is IOException) offline = "Can't reach $server"
                }
            }
        }
    }

    /** Runs [block], showing a failure in the snackbar (or as offline, for connection errors). */
    private fun launch(quiet: Boolean = false, block: suspend () -> Unit) = viewModelScope.launch {
        try {
            block()
        } catch (e: CancellationException) {
            throw e
        } catch (e: ApiException) {
            notices += e.message ?: "Request failed"
        } catch (e: IOException) {
            offline = "Can't reach $server"
            if (!quiet) notices += "Can't reach the server"
        } catch (e: Exception) {
            notices += e.message ?: e.toString()
        }
    }

    /** Runs a change, then reloads everything (the data is small). */
    private fun change(block: suspend () -> Unit) = launch {
        block()
        refresh().join()
    }

    fun notice(text: String) { notices += text }

    // ── Settings ──

    fun configure(newServer: String, newAgent: String): String? {
        val a = runCatching { Api(newServer, newAgent.trim()) }.getOrElse { return it.message }
        if (newAgent.isBlank()) return "Pick a name"
        prefs.server = newServer; prefs.agent = newAgent; prefs.configured = true
        server = prefs.server; agent = prefs.agent; needsSetup = false
        api = a
        poll?.cancel(); poll = null; logRoom = null; log.clear(); prunedLog = null
        previews = emptyMap(); notifiedUpTo.clear()
        refresh()
        syncPoll()
        if (foreground) startTicker()
        return null
    }

    fun setTheme(t: ServerTheme?) {
        themeName = t?.name; themeColors = t?.colors ?: emptyMap()
        prefs.themeName = themeName; prefs.themeColors = themeColors
    }

    fun fileUrl(id: String) = api.fileUrl(id)

    // ── Navigation ──

    fun go(v: View) {
        view = v
        prefs.view = v.key()
    }

    fun setTab(tab: Int) { homeTab = tab; prefs.homeTab = tab }

    fun push(s: Screen) {
        if (stack.lastOrNull() == s) return
        stack += s
        syncPoll()
    }

    fun pop() {
        if (stack.isNotEmpty()) stack.removeAt(stack.lastIndex)
        syncPoll()
    }

    fun openChat(id: Int) {
        Notify.clear(ctx, id)
        // From a notification or a share: whatever was open gives way to the room.
        if (chatRoom != id) stack.removeAll { it is Screen.Chat || it is Screen.RoomTodos || it is Screen.RoomPruned || it is Screen.RoomMedia }
        if (stack.lastOrNull() != Screen.Chat(id)) stack += Screen.Chat(id)
        syncPoll()
    }

    fun toggleShowDone() { showDone = !showDone; prefs.showDone = showDone }

    fun toggleCollapsed(id: Int) {
        collapsed = if (id in collapsed) collapsed - id else collapsed + id
        prefs.collapsed = collapsed
    }

    fun showImages(items: List<GalleryItem>, start: Int) { gallery = Gallery(items, start.coerceIn(0, (items.size - 1).coerceAtLeast(0))) }
    fun closeGallery() { gallery = null }

    // ── Categories ──

    fun cat(id: Int?) = id?.let { i -> cats.find { it.id == i } }
    fun children(parentId: Int?) = cats.filter { it.parentId == parentId }.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.name })

    fun pathOf(id: Int?): List<Category> {
        val out = ArrayList<Category>()
        var c = cat(id)
        while (c != null && out.size < 64) { out.add(0, c); c = cat(c.parentId) }
        return out
    }

    fun pathName(id: Int?, from: Int? = null) = pathOf(id).drop(if (from != null) pathOf(from).size else 0).joinToString(" / ") { it.name }

    fun isWithin(id: Int?, ancestor: Int) = pathOf(id).any { it.id == ancestor }

    /** True if category [id] or one of its ancestors is archived. */
    fun inArchive(id: Int?) = pathOf(id).any { it.archived }

    /** A category's style: its own, else the nearest ancestor's. */
    fun styleOf(id: Int?): CatStyle? = pathOf(id).lastOrNull { it.style != null && !it.style.isEmpty }?.style

    /** Categories below [parentId] in display order, with depth; archived ones only inside the archive. */
    fun flatten(parentId: Int?, skipCollapsed: Boolean = false): List<Pair<Category, Int>> {
        val out = ArrayList<Pair<Category, Int>>()
        fun walk(pid: Int?, depth: Int) {
            val archived = inArchive(pid)
            for (c in children(pid)) {
                if (c.archived && !archived) continue
                out += c to depth
                if (!(skipCollapsed && c.id in collapsed)) walk(c.id, depth + 1)
            }
        }
        walk(parentId, 0)
        return out
    }

    fun archiveRoots() = cats.filter { it.archived && !inArchive(it.parentId) }
        .sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { pathName(it.id) })

    fun addCategory(name: String, parentId: Int?, open: Boolean = true) = change {
        val c = api.addCategory(name.trim(), parentId)
        if (open) go(View.Cat(c.id))
    }

    fun renameCategory(id: Int, name: String) = change { api.updateCategory(id, body("name" to name.trim())) }
    fun moveCategory(id: Int, parentId: Int?) = change { api.updateCategory(id, body("parentId" to parentId)) }
    fun styleCategory(id: Int, style: CatStyle?) = change { api.updateCategory(id, JSONObject().put("style", styleJson(style))) }
    fun archiveCategory(id: Int, archived: Boolean) = change { api.updateCategory(id, body("archived" to archived)) }

    fun deleteCategory(id: Int) = change {
        val parent = cat(id)?.parentId
        api.deleteCategory(id)
        if (view == View.Cat(id)) go(parent?.let { View.Cat(it) } ?: View.All)
    }

    // ── Todos ──

    /** Whether [t] belongs in view [v]; archived todos only show inside archived categories. */
    fun inView(t: Todo, v: View = view): Boolean = when (v) {
        is View.RoomView -> t.roomId == v.id
        View.All -> !inArchive(t.categoryId)
        View.Inbox -> t.categoryId == null
        View.Archive -> inArchive(t.categoryId)
        View.Pruned -> false
        is View.Cat -> isWithin(t.categoryId, v.id) && (inArchive(v.id) || !inArchive(t.categoryId))
    }

    fun openIn(v: View) = todos.count { !it.done && inView(it, v) }

    fun groups(v: View = view): List<Group> {
        val open = todos.filter { !it.done && inView(it, v) }.sortedWith(listOrder)
        fun direct(id: Int?) = open.filter { it.categoryId == id }
        val out = ArrayList<Group>()
        when (v) {
            View.All -> {
                out += Group(null, null, direct(null))
                for ((c, _) in flatten(null)) out += Group(c.id, pathName(c.id), direct(c.id))
            }
            View.Inbox, is View.RoomView -> out += Group(null, null, open)
            View.Archive -> for (root in archiveRoots()) {
                out += Group(root.id, pathName(root.id), direct(root.id))
                for ((c, _) in flatten(root.id)) out += Group(c.id, pathName(c.id), direct(c.id))
            }
            is View.Cat -> {
                out += Group(v.id, null, direct(v.id))
                for ((c, _) in flatten(v.id)) out += Group(c.id, pathName(c.id, v.id), direct(c.id))
            }
            View.Pruned -> {}
        }
        return out.filter { it.todos.isNotEmpty() }
    }

    fun doneInView(v: View = view) = todos.filter { it.done && inView(it, v) }.sortedByDescending { it.id }

    /** Where new todos go in view [v]. */
    private fun target(v: View): Pair<Int?, Int?> = when (v) {
        is View.Cat -> v.id to null
        is View.RoomView -> null to v.id
        else -> null to null
    }

    fun addTodo(title: String, priority: String? = null, deadline: String? = null, description: String? = null, v: View = view) = change {
        val (catId, roomId) = target(v)
        val json = body("title" to title.trim(), "categoryId" to catId)
        if (roomId != null) json.put("roomId", roomId)
        priority?.let { json.put("priority", it) }
        deadline?.let { json.put("deadline", it) }
        description?.takeIf { it.isNotBlank() }?.let { json.put("description", it) }
        api.addTodo(json)
    }

    fun setDone(t: Todo, done: Boolean) {
        // Tick it straight away; the reload confirms it.
        todos = todos.map { if (it.id == t.id) it.copy(done = done) else it }
        change { api.updateTodo(t.id, body("done" to done)) }
    }

    fun saveTodo(id: Int, json: JSONObject) = change { api.updateTodo(id, json) }
    fun deleteTodo(id: Int) = change { api.deleteTodo(id) }
    fun pruneTodo(id: Int) = change { api.pruneTodo(id) }
    fun pruneDone() = change { notices += "Pruned ${api.pruneDone()}" }
    fun claim(t: Todo, force: Boolean = false) {
        todos = todos.map { if (it.id == t.id) it.copy(claimedBy = agent) else it }
        change { api.claim(t.id, force) }
    }
    fun release(t: Todo, force: Boolean = false) {
        todos = todos.map { if (it.id == t.id) it.copy(claimedBy = null) else it }
        change { api.release(t.id, force) }
    }

    /** Uploads files picked for todo [t] and adds them to its attachments. */
    fun attachToTodo(t: Todo, uris: List<Uri>) = launch {
        if (uris.isEmpty()) return@launch
        todoUploading = t.id
        try {
            val added = uris.map { uri ->
                val d = Media.prepare(ctx, uri)
                try { api.upload(d.file, d.name) } finally { Media.discard(listOf(d)) }
            }
            val now = todos.find { it.id == t.id }?.attachments ?: t.attachments
            api.updateTodo(t.id, JSONObject().put("attachments", attachmentsJson(now + added)))
            refresh().join()
        } finally {
            todoUploading = null
        }
    }

    fun removeTodoAttachment(t: Todo, a: Attachment) = change {
        val rest = t.attachments.filter { it != a }
        api.updateTodo(t.id, JSONObject().put("attachments", if (rest.isEmpty()) JSONObject.NULL else attachmentsJson(rest)))
    }

    fun restore(id: Int) = change { notices += "Restored as #${api.restore(id).id}" }
    fun deletePruned(id: Int) = change { api.deletePruned(id) }
    fun deleteAllPruned() = change { api.deleteAllPruned() }

    // ── Rooms ──

    fun room(id: Int?) = id?.let { i -> rooms.find { it.id == i } }
    fun unread(r: Room) = previews[r.id]?.unread ?: if (r.cursor > (lastRead[r.id] ?: 0)) 1 else 0

    /** Pinned rooms first, then the most recently active. */
    fun sortedRooms(): List<Room> = rooms.sortedWith(
        compareByDescending<Room> { it.id in pinned }
            .thenByDescending { previews[it.id]?.last?.createdAt ?: 0L }
            .thenByDescending { it.cursor }
            .thenBy(String.CASE_INSENSITIVE_ORDER) { it.name },
    )

    fun togglePin(id: Int) {
        pinned = if (id in pinned) pinned - id else pinned + id
        prefs.pinned = pinned
    }

    fun markRead(r: Room) {
        lastRead = lastRead + (r.id to r.cursor)
        prefs.lastRead = lastRead
        previews[r.id]?.let { previews = previews + (r.id to it.copy(unread = 0, mentioned = false)) }
        Notify.clear(ctx, r.id)
    }

    fun addRoom(name: String, topic: String) = change {
        val r = api.addRoom(name.trim(), topic.trim())
        refresh().join()
        openChat(r.id)
    }

    fun updateRoom(id: Int, name: String, topic: String) = change { api.updateRoom(id, body("name" to name.trim(), "topic" to topic.trim())) }

    fun deleteRoom(id: Int) = change {
        api.deleteRoom(id)
        if (view == View.RoomView(id)) go(View.All)
        if (pinned.contains(id)) togglePin(id)
    }

    // ── Sending ──

    /** Sends [text] with [files] to room [roomId]; it shows in the log right away, uploading. */
    fun send(roomId: Int, text: String, files: List<Draft>) {
        if (text.isBlank() && files.isEmpty()) return
        val o = Outgoing(nextKey++, roomId, text.trim(), files, System.currentTimeMillis() / 1000)
        outgoing += o
        deliver(o)
    }

    private fun deliver(o: Outgoing) = viewModelScope.launch {
        o.failed = null
        o.progress = 0f
        try {
            val total = o.drafts.sumOf { it.size }.coerceAtLeast(1).toFloat()
            var done = 0L
            val atts = o.drafts.map { d ->
                val a = api.upload(d.file, d.name) { f -> o.progress = (done + f * d.size) / total }
                done += d.size
                a
            }
            o.progress = 1f
            val msg = api.post(o.roomId, o.text, atts)
            outgoing.remove(o)
            Media.discard(o.drafts)
            if (logRoom == o.roomId) addMessages(listOf(msg))
        } catch (e: CancellationException) {
            throw e
        } catch (e: ApiException) {
            o.failed = e.message ?: "Not sent"
        } catch (e: Exception) {
            o.failed = if (e is IOException) "Not sent: can't reach the server" else (e.message ?: "Not sent")
        }
    }

    fun retry(o: Outgoing) { if (o.failed != null) deliver(o) }

    fun discard(o: Outgoing) {
        outgoing.remove(o)
        Media.discard(o.drafts)
    }

    /** Copies picked files in as drafts for room [roomId]'s next message. */
    fun addDrafts(roomId: Int, uris: List<Uri>, fromCamera: Boolean = false) {
        for (uri in uris) {
            preparing++
            launch {
                try {
                    val d = Media.prepare(ctx, uri, fromCamera)
                    val now = drafts[roomId].orEmpty()
                    if (now.size >= MAX_FILES) {
                        Media.discard(listOf(d))
                        notices += "Up to $MAX_FILES files per message"
                    } else {
                        drafts[roomId] = now + d
                    }
                } finally {
                    preparing--
                }
            }
        }
    }

    fun removeDraft(roomId: Int, d: Draft) {
        drafts[roomId] = drafts[roomId].orEmpty() - d
        Media.discard(listOf(d))
    }

    /** Takes the drafts for sending (they belong to the message now). */
    fun takeDrafts(roomId: Int): List<Draft> = drafts.remove(roomId).orEmpty()

    /** Files shared to us from another app: copied in, then we ask which room. */
    fun receiveShare(uris: List<Uri>) {
        if (uris.isEmpty()) return
        launch {
            preparing += uris.size
            val list = ArrayList<Draft>()
            try {
                for (u in uris.take(MAX_FILES)) {
                    try {
                        list += Media.prepare(ctx, u)
                    } catch (e: MediaException) {
                        notices += e.message ?: "Can't send that"
                    }
                }
            } finally {
                preparing -= uris.size
            }
            if (list.isEmpty()) return@launch
            Media.discard(shared)
            shared = list
            stack.remove(Screen.Share)
            push(Screen.Share)
        }
    }

    fun shareTo(roomId: Int) {
        val list = shared
        shared = emptyList()
        stack.remove(Screen.Share)
        drafts[roomId] = (drafts[roomId].orEmpty() + list).take(MAX_FILES)
        openChat(roomId)
    }

    fun cancelShare() {
        Media.discard(shared)
        shared = emptyList()
        stack.remove(Screen.Share)
    }

    // ── Attachments ──

    private fun withFile(a: Attachment, then: suspend (java.io.File) -> Unit) = launch {
        fetching = a.id
        try {
            then(Media.local(ctx, api, a))
        } finally {
            fetching = null
        }
    }

    fun openFile(a: Attachment) = withFile(a) { Media.open(ctx, it, a.type) }
    fun shareFile(a: Attachment) = withFile(a) { Media.share(ctx, it, a.type) }
    fun saveFile(a: Attachment) = withFile(a) {
        Media.saveToGallery(ctx, it, a.type)
        notices += "Saved to Pictures/Todo"
    }

    // ── Pruned messages ──

    // These bump the room's logVersion, so the refresh after them reloads the log.
    fun pruneMessage(m: Message) = change { api.pruneMessage(m.roomId, m.id) }
    fun pruneMessages(roomId: Int) = change { notices += "Pruned ${api.pruneMessages(roomId)} messages" }
    fun restoreMessage(m: Message) = change { notices += "Restored as #${api.restoreMessage(m.roomId, m.id).id}" }
    fun deletePrunedMessage(m: Message) = change { api.deletePrunedMessage(m.roomId, m.id); loadPrunedLog(m.roomId) }
    fun deleteAllPrunedMessages(roomId: Int) = change { api.deleteAllPrunedMessages(roomId); loadPrunedLog(roomId) }

    /** Loads room [roomId]'s pruned messages. */
    fun loadPrunedLog(roomId: Int) = launch {
        val list = api.prunedMessages(roomId)
        prunedLog = list
    }

    // ── The open room ──

    /** Loads the open room's log again from scratch. */
    private fun reloadLog() {
        poll?.cancel(); poll = null
        log.clear()
        syncPoll()
    }

    private fun markLogRead() {
        val id = logRoom ?: return
        val last = log.lastOrNull()?.seq ?: return
        if ((lastRead[id] ?: 0) >= last) return
        lastRead = lastRead + (id to last)
        prefs.lastRead = lastRead
    }

    /** Adds messages not already in the log; true if any were events (a room todo changed). */
    private fun addMessages(msgs: List<Message>): Boolean {
        val have = log.mapTo(HashSet()) { it.seq }
        val fresh = msgs.filter { it.seq !in have }
        if (fresh.isEmpty()) return false
        log.addAll(fresh)
        log.sortBy { it.seq }
        val top = log.last()
        rooms = rooms.map { if (it.id == logRoom && it.cursor < top.seq) it.copy(cursor = top.seq) else it }
        logRoom?.let { id -> previews = previews + (id to Preview(top, 0, false)) }
        if (foreground) markLogRead()
        return fresh.any { it.isEvent }
    }

    /** Fetches the page of the open room's history before what's loaded. */
    fun loadOlder() {
        val id = logRoom ?: return
        if (!hasOlder || loadingOlder) return
        val first = log.firstOrNull()?.seq ?: return
        loadingOlder = true
        launch(quiet = true) {
            try {
                val msgs = api.messages(id, PAGE, before = first)
                if (logRoom == id) {
                    val had = log.size
                    addMessages(msgs)
                    // A server without `before` sends the newest again: nothing new, so stop.
                    hasOlder = msgs.size >= PAGE && log.size > had
                }
            } finally {
                loadingOlder = false
            }
        }
    }

    // ── A room's gallery ──

    fun openMedia(roomId: Int) {
        push(Screen.RoomMedia(roomId))
        if (roomMedia?.roomId == roomId && roomMedia?.filesDone == true) return
        val st = RoomMediaState(roomId)
        roomMedia = st
        launch(quiet = true) {
            // Every file in the room, newest first, a page at a time.
            var before = 0L
            val all = ArrayList<RoomFile>()
            while (roomMedia === st) {
                val page = api.roomFiles(roomId, before, FILES_PAGE)
                all += page
                st.files = all.toList()
                if (page.size < FILES_PAGE) break
                before = page.last().seq
            }
            st.filesDone = true
        }
    }

    /** Finds every link posted in the gallery's room (reading its whole history, a page at a time). */
    fun loadLinks() {
        val st = roomMedia ?: return
        if (st.links != null || st.linksLoading) return
        st.linksLoading = true
        launch(quiet = true) {
            try {
                val found = ArrayList<LinkItem>()
                var before = 0L
                while (roomMedia === st) {
                    val page = if (before == 0L) api.messages(st.roomId, MAX_LIMIT) else api.messages(st.roomId, MAX_LIMIT, before = before)
                    for (m in page.asReversed()) {
                        if (m.isEvent) continue
                        for (u in URL_RE.findAll(m.text)) {
                            val url = u.value.trimEnd('.', ',', ';', ':', '!', '?', ')', ']', '}', '\'', '"')
                            found += LinkItem(if (url.startsWith("www.", true)) "https://$url" else url, m.author, m.createdAt, m.id)
                        }
                    }
                    st.links = found.toList()
                    val oldest = page.firstOrNull()?.seq ?: break
                    // A full page may have more before it; one without `before` support repeats itself.
                    if (page.size < MAX_LIMIT || oldest == before) break
                    before = oldest
                }
                if (st.links == null) st.links = emptyList()
            } finally {
                st.linksLoading = false
            }
        }
    }

    /** Keeps one long poll running for the open room while the app is visible. */
    private fun syncPoll() {
        val id = chatRoom
        if (id == null) {
            poll?.cancel(); poll = null
            if (logRoom != null) { logRoom = null; log.clear(); prunedLog = null; unreadFrom = null }
            return
        }
        if (!foreground || needsSetup) {
            poll?.cancel(); poll = null
            return
        }
        if (poll?.isActive == true && logRoom == id) return
        poll?.cancel()
        if (logRoom != id) {
            log.clear(); logRoom = id; prunedLog = null; logLimit = PAGE; hasOlder = false; logLoaded = false
            unreadFrom = lastRead[id] ?: 0
        }
        markLogRead()
        poll = viewModelScope.launch {
            var backoff = 1000L
            var initial = log.isEmpty()
            while (isActive) {
                try {
                    if (initial) {
                        val msgs = api.messages(id, logLimit)
                        hasOlder = msgs.size >= logLimit
                        addMessages(msgs)
                        logLoaded = true
                        initial = false
                    } else if (addMessages(api.waitMessages(id, log.lastOrNull()?.seq ?: 0, WAIT))) {
                        refresh()
                    }
                    linkUp = true
                    backoff = 1000L
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    // Server down or restarting: wait a bit longer each time.
                    linkUp = false
                    delay(backoff)
                    backoff = (backoff * 2).coerceAtMost(15_000L)
                }
            }
        }
    }

    override fun onCleared() {
        Media.discard(shared)
        super.onCleared()
    }

    companion object {
        private val PRIORITY = mapOf("high" to 3, "medium" to 2, "low" to 1)
        private const val PAGE = 100
        private const val MAX_LIMIT = 1000
        private const val MAX_UNREAD = 100
        const val MAX_FILES = 10
        /** Seconds a live update waits on the server: short enough to notice a dead connection soon. */
        private const val WAIT = 25
        private const val TICK = 5000L
        private const val FILES_PAGE = 500
        private val URL_RE = Regex("""(?i)\b(?:https?://|www\.)[^\s<>"']+""")

        /** Higher priority first, then earlier deadline (none last), then oldest first. */
        val listOrder = Comparator<Todo> { a, b ->
            val p = (PRIORITY[b.priority] ?: 0) - (PRIORITY[a.priority] ?: 0)
            when {
                p != 0 -> p
                a.deadline != b.deadline -> when {
                    a.deadline == null -> 1
                    b.deadline == null -> -1
                    else -> a.deadline.compareTo(b.deadline)
                }
                else -> a.id - b.id
            }
        }
    }
}
