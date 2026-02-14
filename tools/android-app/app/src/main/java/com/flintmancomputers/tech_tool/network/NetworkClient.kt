package com.flintmancomputers.tech_tool.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL
import java.net.UnknownHostException
import java.io.InterruptedIOException
import javax.net.ssl.HttpsURLConnection

object NetworkClient {
    // Map Throwables to concise, user-friendly diagnostic messages
    private fun friendlyForThrowable(t: Throwable): String {
        val m = t.message?.lowercase() ?: ""
        return when (t) {
            is UnknownHostException -> "DNS resolution failed"
            is InterruptedIOException -> "Connection timed out"
            else -> when {
                "unable to resolve host" in m || "unknownhostexception" in m || "cannot resolve" in m || "nodename nor servname provided" in m -> "DNS resolution failed"
                "connect timed out" in m || "connection timed out" in m -> "Connection timed out"
                "failed to connect" in m || "connection refused" in m -> "Connection refused"
                else -> t.message ?: "network error"
            }
        }
    }

    private fun prepareConnection(url: URL, apiKey: String?, connectTimeout: Int = 2000, readTimeout: Int = 2000): HttpURLConnection {
        val conn = url.openConnection() as HttpURLConnection
        conn.connectTimeout = connectTimeout
        conn.readTimeout = readTimeout
        if (apiKey?.isNotBlank() == true) conn.setRequestProperty("X-API-Key", apiKey)
        if (conn is HttpsURLConnection) {
            val (sslFactory, _) = SslUtil.createInsecureSslSocketFactory()
            conn.sslSocketFactory = sslFactory
            conn.hostnameVerifier = SslUtil.permissiveHostnameVerifier
        }
        return conn
    }

    private suspend fun doGetJson(address: String, port: Int, apiKey: String?, path: String, timeoutMs: Int = 2000): JSONObject? = withContext(Dispatchers.IO) {
        // Normalize address: strip any leading scheme and trailing slashes
        val rawAddress = address.trim()
        val host = rawAddress.removePrefix("http://").removePrefix("https://").trimEnd('/')
        val scheme = "https"
        val url = URL("$scheme://$host:$port$path")
        var conn: HttpURLConnection? = null
        try {
            conn = prepareConnection(url, apiKey, timeoutMs, timeoutMs)
            conn.requestMethod = "GET"
            conn.connect()
            val code = conn.responseCode
            if (code in 200..299) {
                return@withContext JSONObject(conn.inputStream.bufferedReader().use { it.readText() })
            }
        } catch (_: Throwable) {
            // no http fallback: report null so caller can react
        } finally {
            try { conn?.disconnect() } catch (_: Throwable) {}
        }
        null
    }

    private suspend fun doPostJson(address: String, port: Int, apiKey: String?, path: String, bodyJson: String, timeoutMs: Int = 3000): JSONObject? = withContext(Dispatchers.IO) {
        val rawAddress = address.trim()
        val host = rawAddress.removePrefix("http://").removePrefix("https://").trimEnd('/')
        val scheme = "https"
        val url = URL("$scheme://$host:$port$path")
        var conn: HttpURLConnection? = null
        try {
            conn = prepareConnection(url, apiKey, timeoutMs, timeoutMs)
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            OutputStreamWriter(conn.outputStream).use { it.write(bodyJson) }
            val code = conn.responseCode
            if (code in 200..299) return@withContext JSONObject(conn.inputStream.bufferedReader().use { it.readText() })
        } catch (_: Throwable) {
            // no http fallback
        } finally {
            try { conn?.disconnect() } catch (_: Throwable) {}
        }
        null
    }

    // Do a GET and return raw text body or an error message (Pair<content, error>)
    private suspend fun doGetText(address: String, port: Int, apiKey: String?, path: String, timeoutMs: Int = 3000): Pair<String?, String?> = withContext(Dispatchers.IO) {
        val rawAddress = address.trim()
        val host = rawAddress.removePrefix("http://").removePrefix("https://").trimEnd('/')
        val scheme = "https"
        val url = URL("$scheme://$host:$port$path")
        var conn: HttpURLConnection? = null
        try {
            conn = prepareConnection(url, apiKey, timeoutMs, timeoutMs)
            conn.requestMethod = "GET"
            conn.connect()
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream ?: conn.inputStream
            val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
            if (code in 200..299) return@withContext Pair(text, null)
            val lastErr = "HTTP $code: ${if (text.length > 1000) text.take(1000) + "..." else text}"
            return@withContext Pair(null, lastErr)
        } catch (t: Throwable) {
            val lastErr = friendlyForThrowable(t)
            return@withContext Pair(null, lastErr)
        } finally {
            try { conn?.disconnect() } catch (_: Throwable) {}
        }
    }

    // Fetch events log for a specific date (YYYY-MM-DD). Returns Pair<content, error>.
    suspend fun getEventsLog(address: String, port: Int, apiKey: String?, date: String): Pair<String?, String?> {
        val path = "/api/v1/logs/events?date=$date"
        return doGetText(address, port, apiKey, path)
    }

    suspend fun getSystemInfo(address: String, port: Int, apiKey: String?): JSONObject? = doGetJson(address, port, apiKey, "/api/v1/system-info")

    suspend fun postSetpoint(address: String, port: Int, apiKey: String?, setpoint: Double): JSONObject? =
        doPostJson(address, port, apiKey, "/api/v1/setpoint", JSONObject().put("setpoint", setpoint).toString())

    suspend fun postResetAlarms(address: String, port: Int, apiKey: String?): JSONObject? =
        doPostJson(address, port, apiKey, "/api/v1/alarms/reset", "{}")

    suspend fun postTriggerDefrost(address: String, port: Int, apiKey: String?): JSONObject? =
        doPostJson(address, port, apiKey, "/api/v1/defrost/trigger", "{}")

    suspend fun getDemoMode(address: String, port: Int, apiKey: String?): JSONObject? = doGetJson(address, port, apiKey, "/api/v1/demo-mode")

    suspend fun postDemoMode(address: String, port: Int, apiKey: String?, enable: Boolean): JSONObject? =
        doPostJson(address, port, apiKey, "/api/v1/demo-mode", JSONObject().put("enable", enable).toString())

    // Post configuration updates to /api/v1/config. Accepts a map of key->value (strings).
    suspend fun postConfig(address: String, port: Int, apiKey: String?, updates: Map<String, String>): JSONObject? {
        val body = JSONObject()
        updates.forEach { (k, v) -> body.put(k, v) }
        return doPostJson(address, port, apiKey, "/api/v1/config", body.toString())
    }
}
