package dev.vallhala.todo

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat

/** Local notifications for messages that @mention us (seen while the app is running). */
object Notify {
    private const val CHANNEL = "mentions"
    const val EXTRA_ROOM = "dev.vallhala.todo.room"

    fun allowed(ctx: Context) = Build.VERSION.SDK_INT < 33 ||
        ContextCompat.checkSelfPermission(ctx, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    private fun channel(ctx: Context) {
        val nm = ctx.getSystemService(NotificationManager::class.java)
        if (nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL, "Mentions", NotificationManager.IMPORTANCE_HIGH).apply {
                    description = "Messages in rooms that mention you"
                },
            )
        }
    }

    fun mention(ctx: Context, room: Room, m: Message) {
        if (!allowed(ctx)) return
        channel(ctx)
        val open = Intent(ctx, MainActivity::class.java)
            .putExtra(EXTRA_ROOM, room.id)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        val pi = PendingIntent.getActivity(ctx, room.id, open, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val text = m.text.ifBlank { if (m.attachments.any { it.isImage }) "📷 Photo" else "📄 File" }
        val n = NotificationCompat.Builder(ctx, CHANNEL)
            .setSmallIcon(R.drawable.ic_notify)
            .setContentTitle("${m.author} in ${room.name}")
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(pi)
            .setAutoCancel(true)
            .setCategory(NotificationCompat.CATEGORY_MESSAGE)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .build()
        try {
            NotificationManagerCompat.from(ctx).notify(room.id, n)
        } catch (_: SecurityException) {
            // Permission withdrawn in the meantime.
        }
    }

    fun clear(ctx: Context, roomId: Int) = NotificationManagerCompat.from(ctx).cancel(roomId)
}
