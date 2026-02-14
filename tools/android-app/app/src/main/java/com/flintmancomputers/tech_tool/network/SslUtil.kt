package com.flintmancomputers.tech_tool.network

import java.security.SecureRandom
import javax.net.ssl.*
import okhttp3.OkHttpClient

object SslUtil {
    fun createInsecureSslSocketFactory(): Pair<SSLSocketFactory, X509TrustManager> {
        val trustAllCerts = arrayOf<TrustManager>(object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<java.security.cert.X509Certificate>, authType: String) {}
            override fun checkServerTrusted(chain: Array<java.security.cert.X509Certificate>, authType: String) {}
            override fun getAcceptedIssuers(): Array<java.security.cert.X509Certificate> = arrayOf()
        })

        val sslContext = SSLContext.getInstance("TLS")
        sslContext.init(null, trustAllCerts, SecureRandom())
        val tm = trustAllCerts[0] as X509TrustManager
        return Pair(sslContext.socketFactory, tm)
    }

    val permissiveHostnameVerifier = HostnameVerifier { _, _ -> true }

    fun createInsecureOkHttpClient(): OkHttpClient {
        val (sslFactory, tm) = createInsecureSslSocketFactory()
        return OkHttpClient.Builder()
            .sslSocketFactory(sslFactory, tm)
            .hostnameVerifier(permissiveHostnameVerifier)
            .build()
    }
}
