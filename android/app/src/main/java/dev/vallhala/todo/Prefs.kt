package dev.vallhala.todo

import android.content.Context
import android.os.Build
import org.json.JSONObject

/** Settings and small bits of state kept on the phone. */
class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("todo", Context.MODE_PRIVATE)

    var server: String
        get() = sp.getString("server", null) ?: "http://navrobotec:27183"
        set(v) = sp.edit().putString("server", v.trim()).apply()

    /** Who we are in rooms. */
    var agent: String
        get() = sp.getString("agent", null) ?: defaultAgent()
        set(v) = sp.edit().putString("agent", v.trim()).apply()

    var configured: Boolean
        get() = sp.getBoolean("configured", false)
        set(v) = sp.edit().putBoolean("configured", v).apply()

    /** The server theme's name and colors (kept so the app starts in them), or null for the system's. */
    var themeName: String?
        get() = sp.getString("theme", null)
        set(v) = sp.edit().putString("theme", v).apply()

    var themeColors: Map<String, String>
        get() = sp.getString("themeColors", null)?.let { s ->
            runCatching { JSONObject(s).let { o -> o.keys().asSequence().associateWith { o.getString(it) } } }.getOrNull()
        } ?: emptyMap()
        set(v) = sp.edit().putString("themeColors", JSONObject(v).toString()).apply()

    var view: String
        get() = sp.getString("view", null) ?: "all"
        set(v) = sp.edit().putString("view", v).apply()

    var showDone: Boolean
        get() = sp.getBoolean("showDone", false)
        set(v) = sp.edit().putBoolean("showDone", v).apply()

    /** Whether a room shows its whole topic and members, or just a line. */
    var roomHeader: Boolean
        get() = sp.getBoolean("roomHeader", false)
        set(v) = sp.edit().putBoolean("roomHeader", v).apply()

    var collapsed: Set<Int>
        get() = sp.getStringSet("collapsed", emptySet())!!.mapNotNull { it.toIntOrNull() }.toSet()
        set(v) = sp.edit().putStringSet("collapsed", v.map { it.toString() }.toSet()).apply()

    /** The last message id read in each room, for the unread dots. */
    var lastRead: Map<Int, Long>
        get() = sp.getString("lastRead", null)?.let { s ->
            runCatching { JSONObject(s).let { o -> o.keys().asSequence().associate { it.toInt() to o.getLong(it) } } }.getOrNull()
        } ?: emptyMap()
        set(v) = sp.edit().putString("lastRead", JSONObject(v.mapKeys { it.key.toString() }).toString()).apply()

    private fun defaultAgent() = Build.MODEL.lowercase().replace(Regex("[^a-z0-9]+"), "-").trim('-').ifEmpty { "phone" }
}
