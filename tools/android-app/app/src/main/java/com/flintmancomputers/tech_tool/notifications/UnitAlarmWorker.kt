package com.flintmancomputers.tech_tool.notifications

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import com.flintmancomputers.tech_tool.units.UnitsDatabase
import com.flintmancomputers.tech_tool.network.UnitPoller
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

class UnitAlarmWorker(
    appContext: Context,
    workerParams: WorkerParameters
) : CoroutineWorker(appContext, workerParams) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val context = applicationContext
        val db = UnitsDatabase.getInstance(context)
        val dao = db.unitDao()
        val poller = UnitPoller()
        val units = dao.getAllList() // suspend function returns List<UnitEntity>
        var anyError = false
        for (unit in units) {
            try {
                val res = poller.poll(unit)
                val body = res.body
                var inAlarm = false
                var alarmText: String? = null
                if (!body.isNullOrBlank()) {
                    try {
                        val j = JSONObject(body)
                        val activeAlarms = j.optJSONArray("active_alarms")
                        if (activeAlarms != null && activeAlarms.length() > 0) {
                            inAlarm = true
                            val codes = (0 until activeAlarms.length()).mapNotNull { idx -> activeAlarms.optString(idx) }
                            alarmText = "Active alarms: ${codes.joinToString(", ")}" }
                        else {
                            val shutdown = j.optBoolean("alarm_shutdown", false)
                            val warning = j.optBoolean("alarm_warning", false)
                            if (shutdown) { inAlarm = true; alarmText = "Shutdown alarm" }
                            else if (warning) { inAlarm = true; alarmText = "Warning alarm" }
                        }
                    } catch (_: Throwable) {}
                }
                if (inAlarm) {
                    NotificationHelper.notifyAlarm(context, unit, alarmText ?: "Alarm on unit ${unit.unitId}")
                } else {
                    NotificationHelper.clearAlarm(context, unit.id)
                }
            } catch (_: Exception) {
                anyError = true
            }
        }
        if (anyError) Result.retry() else Result.success()
    }
}
