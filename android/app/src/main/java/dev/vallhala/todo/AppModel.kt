package dev.vallhala.todo

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
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

class AppModel(app: Application) : AndroidViewModel(app) {
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
    var roomHeader by mutableStateOf(prefs.roomHeader); private set
    var lastRead by mutableStateOf(prefs.lastRead); private set

    var loading by mutableStateOf(false); private set
    var loaded by mutableStateOf(false); private set
    /** Set while the server can't be reached; cleared by the next successful load. */
    var offline by mutableStateOf<String?>(null); private set
    /** One-off messages for the snackbar. */
    val notices = mutableStateListOf<String>()

    /** The open room's log. */
    val log = mutableStateListOf<Message>()
    private var logRoom: Int? = null
    private var poll: Job? = null
    private var foreground = false

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
            todos = t; cats = c; rooms = r; pruned = p
            offline = null
            loaded = true
            // A view that no longer exists (deleted elsewhere) falls back to All.
            val v = view
            if ((v is View.Cat && cats.none { it.id == v.id }) || (v is View.RoomView && rooms.none { it.id == v.id })) go(View.All)
        } finally {
            loading = false
        }
        runCatching { api.themes() }.getOrNull()?.let { list ->
            themes = list
            // Pick up edits to the chosen theme's file.
            list.find { it.name == themeName }?.let { setTheme(it) }
        }
    }

    /** Called when the app comes to the front or goes away: rooms are only polled while it's visible. */
    fun setForeground(on: Boolean) {
        foreground = on
        if (on) {
            if (!needsSetup) refresh()
            syncPoll()
        } else {
            poll?.cancel(); poll = null
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

    // ── Settings ──

    fun configure(newServer: String, newAgent: String): String? {
        val a = runCatching { Api(newServer, newAgent.trim()) }.getOrElse { return it.message }
        if (newAgent.isBlank()) return "Pick a name"
        prefs.server = newServer; prefs.agent = newAgent; prefs.configured = true
        server = prefs.server; agent = prefs.agent; needsSetup = false
        api = a
        poll?.cancel(); poll = null; logRoom = null; log.clear()
        refresh()
        syncPoll()
        return null
    }

    fun setTheme(t: ServerTheme?) {
        themeName = t?.name; themeColors = t?.colors ?: emptyMap()
        prefs.themeName = themeName; prefs.themeColors = themeColors
    }

    // ── Navigation ──

    fun go(v: View) {
        view = v
        prefs.view = v.key()
        syncPoll()
    }

    fun toggleShowDone() { showDone = !showDone; prefs.showDone = showDone }

    fun toggleRoomHeader() { roomHeader = !roomHeader; prefs.roomHeader = roomHeader }

    fun toggleCollapsed(id: Int) {
        collapsed = if (id in collapsed) collapsed - id else collapsed + id
        prefs.collapsed = collapsed
    }

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

    fun groups(): List<Group> {
        val open = todos.filter { !it.done && inView(it) }.sortedWith(listOrder)
        fun direct(id: Int?) = open.filter { it.categoryId == id }
        val out = ArrayList<Group>()
        when (val v = view) {
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

    fun doneInView() = todos.filter { it.done && inView(it) }.sortedByDescending { it.id }

    /** Where new todos go in the current view. */
    private fun target(): Pair<Int?, Int?> = when (val v = view) {
        is View.Cat -> v.id to null
        is View.RoomView -> null to v.id
        else -> null to null
    }

    fun addTodo(title: String, priority: String? = null, deadline: String? = null, description: String? = null) = change {
        val (catId, roomId) = target()
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
    fun claim(t: Todo, force: Boolean = false) = change { api.claim(t.id, force) }
    fun release(t: Todo, force: Boolean = false) = change { api.release(t.id, force) }

    fun restore(id: Int) = change { notices += "Restored as #${api.restore(id).id}" }
    fun deletePruned(id: Int) = change { api.deletePruned(id) }
    fun deleteAllPruned() = change { api.deleteAllPruned() }

    // ── Rooms ──

    fun room(id: Int?) = id?.let { i -> rooms.find { it.id == i } }
    fun unread(r: Room) = r.lastMessageId > (lastRead[r.id] ?: 0)

    fun addRoom(name: String, topic: String) = change {
        val r = api.addRoom(name.trim(), topic.trim())
        go(View.RoomView(r.id))
    }

    fun updateRoom(id: Int, name: String, topic: String) = change { api.updateRoom(id, body("name" to name.trim(), "topic" to topic.trim())) }

    fun deleteRoom(id: Int) = change {
        api.deleteRoom(id)
        if (view == View.RoomView(id)) go(View.All)
    }

    fun post(text: String, onSent: () -> Unit) = launch {
        val id = (view as? View.RoomView)?.id ?: return@launch
        val msg = api.post(id, text.trim())
        onSent()
        if (logRoom == id) addMessages(listOf(msg))
    }

    private fun markRead() {
        val id = logRoom ?: return
        val last = log.lastOrNull()?.id ?: return
        if ((lastRead[id] ?: 0) >= last) return
        lastRead = lastRead + (id to last)
        prefs.lastRead = lastRead
    }

    /** Adds messages not already in the log; true if any were events (a room todo changed). */
    private fun addMessages(msgs: List<Message>): Boolean {
        val have = log.mapTo(HashSet()) { it.id }
        val fresh = msgs.filter { it.id !in have }
        if (fresh.isEmpty()) return false
        log.addAll(fresh)
        log.sortBy { it.id }
        val top = fresh.maxOf { it.id }
        rooms = rooms.map { if (it.id == logRoom && it.lastMessageId < top) it.copy(lastMessageId = top) else it }
        if (foreground) markRead()
        return fresh.any { it.isEvent }
    }

    /** Keeps one long poll running for the open room while the app is visible. */
    private fun syncPoll() {
        val id = (view as? View.RoomView)?.id
        if (!foreground || id == null || needsSetup) {
            poll?.cancel(); poll = null
            return
        }
        if (poll?.isActive == true && logRoom == id) return
        poll?.cancel()
        if (logRoom != id) { log.clear(); logRoom = id }
        markRead()
        poll = viewModelScope.launch {
            var backoff = 1000L
            var initial = log.isEmpty()
            while (isActive) {
                try {
                    if (initial) {
                        addMessages(api.messages(id))
                        initial = false
                    } else if (addMessages(api.waitMessages(id, log.lastOrNull()?.id ?: 0))) {
                        refresh()
                    }
                    backoff = 1000L
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    // Server down or restarting: wait a bit longer each time.
                    delay(backoff)
                    backoff = (backoff * 2).coerceAtMost(30_000L)
                }
            }
        }
    }

    companion object {
        private val PRIORITY = mapOf("high" to 3, "medium" to 2, "low" to 1)

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
