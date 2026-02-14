package com.flintmancomputers.tech_tool.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.UnknownHostException
import java.io.InterruptedIOException
import javax.net.ssl.HttpsURLConnection

suspend fun fetchSystemStatus(
    apiAddress: String,
    apiPort: Int,
    apiKey: String? = null,
    timeoutMs: Int = 2000
): JSONObject? = withContext(Dispatchers.IO) {
    fetchSystemStatusWithError(apiAddress, apiPort, apiKey, timeoutMs).first
}

// New: returns Pair<JSONObject?, diagnosticMessage>. diagnosticMessage contains the last error or the non-success response body.
suspend fun fetchSystemStatusWithError(
    apiAddress: String,
    apiPort: Int,
    apiKey: String? = null,
    timeoutMs: Int = 2000
): Pair<JSONObject?, String?> = withContext(Dispatchers.IO) {
    val path = "/api/v1/status"
    var lastErr: String? = null
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
    // Normalize address
    val raw = apiAddress.trim()
    val host = raw.removePrefix("http://").removePrefix("https://").trimEnd('/')
    val scheme = "https"
    val url = URL("$scheme://$host:$apiPort$path")
    var conn: HttpURLConnection? = null
    try {
        conn = (url.openConnection() as HttpURLConnection).apply {
            connectTimeout = timeoutMs
            readTimeout = timeoutMs
            requestMethod = "GET"
            if (apiKey?.isNotBlank() == true) setRequestProperty("X-API-Key", apiKey)
        }
        if (conn is HttpsURLConnection) {
            val (sslFactory, _) = SslUtil.createInsecureSslSocketFactory()
            conn.sslSocketFactory = sslFactory
            conn.hostnameVerifier = SslUtil.permissiveHostnameVerifier
        }
        conn.connect()
        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream ?: conn.inputStream
        val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
        if (code in 200..299) {
            return@withContext Pair(JSONObject(text), null)
        } else {
            lastErr = "HTTP $code: ${if (text.length > 1000) text.take(1000) + "..." else text}"
        }
    } catch (t: Throwable) {
        lastErr = friendlyForThrowable(t)
    } finally {
        try { conn?.disconnect() } catch (_: Throwable) {}
    }
    return@withContext Pair(null, lastErr)
}
