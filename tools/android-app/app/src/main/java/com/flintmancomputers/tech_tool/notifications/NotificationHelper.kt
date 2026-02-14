package com.flintmancomputers.tech_tool.notifications

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
import com.flintmancomputers.tech_tool.MainActivity
import com.flintmancomputers.tech_tool.R
import com.flintmancomputers.tech_tool.units.UnitEntity

object NotificationHelper {
    private const val CHANNEL_ID = "unit_alarms_channel"
    private const val CHANNEL_NAME = "Unit Alarms"

    // Public wrapper used by other parts of the app to ensure the channel exists
    fun createChannel(context: Context) {
        ensureChannel(context.applicationContext)
    }

    private fun ensureChannel(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = context.getSystemService(NotificationManager::class.java) ?: return
            if (nm.getNotificationChannel(CHANNEL_ID) == null) {
                val channel = NotificationChannel(
                    CHANNEL_ID,
                    CHANNEL_NAME,
                    NotificationManager.IMPORTANCE_HIGH
                ).apply {
                    description = "Notifications for unit alarms"
                    enableLights(true)
                    enableVibration(true)
                }
                nm.createNotificationChannel(channel)
            }
        }
    }

    private fun hasNotificationPermission(context: Context): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.POST_NOTIFICATIONS
            ) == PackageManager.PERMISSION_GRANTED
        }
        return NotificationManagerCompat.from(context).areNotificationsEnabled()
    }

    private fun pendingIntentForMain(context: Context, unitId: Long): PendingIntent {
        val intent = Intent(context, MainActivity::class.java).apply {
            action = Intent.ACTION_MAIN
            addCategory(Intent.CATEGORY_LAUNCHER)
            putExtra("unit_open_id", unitId)
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0
        return PendingIntent.getActivity(context, unitId.hashCode(), intent, flags)
    }

    fun notifyAlarm(context: Context, unit: UnitEntity, text: String) {
        val ctx = context.applicationContext
        ensureChannel(ctx)
        if (!hasNotificationPermission(ctx)) {
            // Permission not granted; nothing we can do from background. App should request permission when opened.
            return
        }

        val builder = NotificationCompat.Builder(ctx, CHANNEL_ID)
            .setSmallIcon(R.mipmap.ic_refrigeration)
            .setContentTitle("Alarm: ${unit.unitId}")
            .setContentText(text)
            .setContentIntent(pendingIntentForMain(ctx, unit.id))
            .setAutoCancel(true)
            .setPriority(NotificationCompat.PRIORITY_HIGH)

        NotificationManagerCompat.from(ctx).notify(unit.id.toInt(), builder.build())
    }

    fun clearAlarm(context: Context, notificationId: Long) {
        NotificationManagerCompat.from(context.applicationContext).cancel(notificationId.toInt())
    }
}
