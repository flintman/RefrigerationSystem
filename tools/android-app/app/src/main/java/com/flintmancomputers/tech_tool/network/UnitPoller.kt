package com.flintmancomputers.tech_tool.network

import android.util.Log
import com.flintmancomputers.tech_tool.units.UnitEntity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL
import java.net.UnknownHostException
import javax.net.ssl.HttpsURLConnection
import java.io.InterruptedIOException

/**
 * Result of a single health check poll.
 * - online: true when HTTP 2xx
 * - body: response body if available (success or error body), or an error message when exception occurred
 * - statusCode: HTTP status code if available
 * - endpoint: which path was called (e.g. /api/v1/health or /api/v1/status)
 */
data class PollResult(val online: Boolean, val body: String?, val statusCode: Int?, val endpoint: String?)

class UnitPoller {
    companion object { private const val TAG = "UnitPoller" }

    suspend fun poll(unit: UnitEntity, timeoutMs: Int = 3000): PollResult = withContext(Dispatchers.IO) {
        // helper to perform a single GET and return PollResult
        fun doGet(path: String): PollResult {
            // Helper to map Throwable to concise user-friendly messages
            fun friendlyForThrowable(t: Throwable): String {
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
             // Normalize address: strip any leading scheme and trailing slashes
             val rawAddress = unit.apiAddress.trim()
             val address = rawAddress.removePrefix("http://").removePrefix("https://").trimEnd('/')
             val fullUrl = "https://$address:${unit.apiPort}$path"
             Log.d(TAG, "doGet: requesting $fullUrl")
             try {
                 val port = unit.apiPort
                 var lastErrorResult: PollResult? = null
                 // Only attempt HTTPS to avoid cleartext HTTP failures. If your server only supports HTTP,
                 // enable cleartext or supply an HTTPS-capable endpoint.
                 val scheme = "https"
                 val tryUrl = URL("$scheme://$address:$port$path")
                 var conn: HttpURLConnection? = null
                 try {
                     conn = (tryUrl.openConnection() as HttpURLConnection).apply {
                         connectTimeout = timeoutMs
                         readTimeout = timeoutMs
                         requestMethod = "GET"
                         if (unit.apiKey.isNotBlank()) setRequestProperty("X-API-Key", unit.apiKey)
                     }
                     if (conn is HttpsURLConnection) {
                         val (sslFactory, _) = SslUtil.createInsecureSslSocketFactory()
                         conn.sslSocketFactory = sslFactory
                         conn.hostnameVerifier = SslUtil.permissiveHostnameVerifier
                     }
                     conn.connect()
                     val code = conn.responseCode
                     Log.d(TAG, "doGet: response code=$code for $tryUrl")
                     val stream = if (code in 200..299) conn.inputStream else conn.errorStream ?: conn.inputStream
                     val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
                     Log.d(TAG, "doGet: body for $tryUrl -> ${if (text.length > 200) text.take(200) + "..." else text}")
                     val result = PollResult(code in 200..299, if (text.isBlank()) null else text, code, path)
                     if (result.online) return result
                     lastErrorResult = result
                 } catch (t: Throwable) {
                     Log.w(TAG, "doGet: exception for $tryUrl", t)
                     lastErrorResult = PollResult(false, friendlyForThrowable(t), null, path)
                 } finally {
                     try { conn?.disconnect() } catch (_: Throwable) {}
                 }
                 // return most informative failure
                 return lastErrorResult ?: PollResult(false, "unknown network error", null, path)
             } catch (t: Throwable) {
                 Log.w(TAG, "doGet: exception for $fullUrl", t)
                 return PollResult(false, friendlyForThrowable(t), null, path)
             }
        }

        // Try status, with one quick retry on network errors (statusCode == null indicates network failure)
        suspend fun tryGet(path: String): PollResult {
            val first = doGet(path)
            if (!first.online && first.statusCode == null) {
                try { delay(250) } catch (_: Throwable) {}
                return doGet(path)
            }
            return first
        }

        // Prefer the fuller status endpoint which contains `system_status` when available
        val status = tryGet("/api/v1/status")
        if (status.online) return@withContext status

        // Fallback to health if status didn't return 2xx (also use tryGet)
        val health = tryGet("/api/v1/health")
        if (health.online) return@withContext health

        // prefer to return the most informative non-online result (status body > health body > network error)
        return@withContext status.body?.let { status } ?: health
    }
}
