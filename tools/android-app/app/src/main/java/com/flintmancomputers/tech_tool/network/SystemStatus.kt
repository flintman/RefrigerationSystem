package com.flintmancomputers.tech_tool.network

import org.json.JSONObject

data class SystemStatus(
    val timestamp: Long?,
    val system: String?,
    val version: String?,
    val relays: Map<String, Boolean>,
    val systemStatus: String?,
    val sensors: Map<String, Double>,
    val setpoint: Double?
) {
    companion object {
        fun fromJson(j: JSONObject): SystemStatus {
            val timestamp = if (j.has("timestamp")) j.optLong("timestamp") else null
            val system = j.optString("system", null)
            val version = j.optString("version", null)
            val systemStatus = j.optString("system_status", null)
            val setpoint = if (j.has("setpoint")) j.optDouble("setpoint") else null

            val relays = mutableMapOf<String, Boolean>()
            if (j.has("relays")) {
                val r = j.getJSONObject("relays")
                r.keys().forEach { k -> relays[k] = r.optBoolean(k, false) }
            }

            val sensors = mutableMapOf<String, Double>()
            if (j.has("sensors")) {
                val s = j.getJSONObject("sensors")
                s.keys().forEach { k -> sensors[k] = s.optDouble(k, Double.NaN) }
            }

            return SystemStatus(
                timestamp = timestamp,
                system = system,
                version = version,
                relays = relays,
                systemStatus = systemStatus,
                sensors = sensors,
                setpoint = if (j.has("setpoint")) j.optDouble("setpoint") else null
            )
        }
    }
}
