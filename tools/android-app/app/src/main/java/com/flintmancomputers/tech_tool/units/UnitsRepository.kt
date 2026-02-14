package com.flintmancomputers.tech_tool.units

import com.flintmancomputers.tech_tool.network.SslUtil
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

class UnitsRepository(private val dao: UnitDao) {

    fun observeAll() = dao.getAll()

    suspend fun insert(unit: UnitEntity) = dao.insert(unit)
    suspend fun update(unit: UnitEntity) = dao.update(unit)
    suspend fun delete(unit: UnitEntity) = dao.delete(unit)
    suspend fun getById(id: Long) = dao.getById(id)

    suspend fun getMaxPosition(): Int = dao.getMaxPosition()

    /**
     * Bulk update units - useful to persist new ordering. This will call update for each unit.
     */
    suspend fun updatePositions(units: List<UnitEntity>) {
        withContext(Dispatchers.IO) {
            units.forEach { dao.update(it) }
        }
    }

    private val client: OkHttpClient by lazy {
        // Use a permissive OkHttp client that accepts self-signed certs for HTTPS
        SslUtil.createInsecureOkHttpClient().newBuilder().callTimeout(15, TimeUnit.SECONDS).build()
    }

    // Generic request helper - try HTTPS first (accepting self-signed certs), fall back to HTTP
    suspend fun sendRequest(unit: UnitEntity, endpointPath: String, method: String = "GET", bodyJson: String? = null): Result<String> {
        return withContext(Dispatchers.IO) {
            val schemes = listOf("https", "http")
            val base = if (unit.apiPort <= 0) unit.apiAddress else "${unit.apiAddress}:${unit.apiPort}"
            var lastException: Exception? = null
            var lastNonSuccessBody: String? = null

            for (scheme in schemes) {
                try {
                    val url = if (endpointPath.startsWith("/")) "$scheme://$base$endpointPath" else "$scheme://$base/$endpointPath"
                    val builder = Request.Builder()
                        .url(url)
                        .addHeader("X-API-Key", unit.apiKey)

                    if (method.equals("POST", ignoreCase = true)) {
                        val body = (bodyJson ?: "{}").toRequestBody("application/json".toMediaTypeOrNull())
                        builder.post(body)
                    } else {
                        builder.get()
                    }

                    val req = builder.build()
                    val resp = client.newCall(req).execute()
                    val text = resp.body?.string() ?: ""
                    if (resp.isSuccessful) return@withContext Result.success(text)
                    // record non-success body and try fallback
                    lastNonSuccessBody = text
                } catch (ex: Exception) {
                    lastException = ex
                    // try next scheme
                }
            }

            // If we reach here no scheme returned success
            lastNonSuccessBody?.let { return@withContext Result.failure(Exception("HTTP error: $it")) }
            lastException?.let { return@withContext Result.failure(it) }
            return@withContext Result.failure(Exception("Unknown network error"))
        }
    }
}
