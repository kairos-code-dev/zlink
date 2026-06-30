package systems.zlink.e2e.kotlin.registrymessaging.consumer.Endpoints

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.time.Duration
import java.util.concurrent.Executors
import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.e2e.kotlin.registrymessaging.consumer.Configuration.ConsumerOptions
import systems.zlink.e2e.kotlin.registrymessaging.shared.Contracts
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileMsg
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.RequestFailureRes
import systems.zlink.framework.channels.ZLinkClient

class ConsumerEndpoints(
    private val options: ConsumerOptions,
    private val context: ConfigurableApplicationContext,
) {
    private val mapper = jacksonObjectMapper()
    private val channels = context.getBean(ZLinkClient::class.java)

    fun start(): HttpServer {
        val uri = URI.create(options.httpUrl)
        val server = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        server.executor = Executors.newCachedThreadPool()
        server.createContext("/health") { exchange ->
            exchange.writeJson(mapOf("status" to "ready", "role" to "consumer"))
        }
        server.createContext("/profile/batch-request") { exchange ->
            val requests = exchange.readJson<Array<ProfileReq>>()
            exchange.writeJson(requests.map { requestProfile(it, Duration.ofSeconds(5)) })
        }
        server.createContext("/profile/request") { exchange ->
            exchange.writeJson(requestProfile(exchange.readJson(), Duration.ofSeconds(5)))
        }
        server.createContext("/profile/slow-request") { exchange ->
            val request = exchange.readJson<ProfileReq>()
            try {
                requestProfile(request, Duration.ofMillis(100), retryUntilReady = false)
                exchange.writeJson(RequestFailureRes(false, ""))
            } catch (error: RuntimeException) {
                exchange.writeJson(RequestFailureRes(true, rootName(error)))
            }
        }
        server.createContext("/profile/missing-request") { exchange ->
            val request = exchange.readJson<ProfileReq>()
            try {
                channels.requestToChannel(Contracts.PROFILE_CHANNEL, request)
                    .packetName("MissingProfileReq")
                    .timeout(Duration.ofSeconds(5))
                    .await(ProfileRes::class.java)
                exchange.writeJson(RequestFailureRes(false, ""))
            } catch (error: RuntimeException) {
                exchange.writeJson(RequestFailureRes(true, rootName(error)))
            }
        }
        server.createContext("/profile/missing-command") { exchange ->
            val command = exchange.readJson<ProfileMsg>()
            channels.sendToChannel(Contracts.PROFILE_CHANNEL, command)
                .packetName("MissingProfileMsg")
                .await()
            exchange.writeJson(mapOf("status" to "sent"))
        }
        server.createContext("/profile/payload") { exchange ->
            exchange.writeJson(requestPayload(exchange.readJson()))
        }
        server.createContext("/shutdown") { exchange ->
            exchange.writeJson(mapOf("status" to "stopping"))
            Thread {
                server.stop(0)
                context.close()
            }.start()
        }
        server.start()
        return server
    }

    private fun requestProfile(
        request: ProfileReq,
        timeout: Duration,
        retryUntilReady: Boolean = true,
    ): ProfileRes {
        val deadline = System.nanoTime() + Duration.ofSeconds(30).toNanos()
        var last: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                return channels.requestToChannel(Contracts.PROFILE_CHANNEL, request)
                    .packetName(Contracts.PROFILE_REQUEST_PACKET)
                    .timeout(timeout)
                    .await(ProfileRes::class.java)
            } catch (error: RuntimeException) {
                if (!retryUntilReady) {
                    throw error
                }
                last = error
                Thread.sleep(100)
            }
        }
        throw IllegalStateException("Timed out waiting for profile endpoint.", last)
    }

    private fun requestPayload(request: PayloadReq): PayloadRes {
        val deadline = System.nanoTime() + Duration.ofSeconds(30).toNanos()
        var last: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                return channels.requestToChannel(Contracts.PROFILE_CHANNEL, request)
                    .packetName(Contracts.PAYLOAD_REQUEST_PACKET)
                    .timeout(Duration.ofSeconds(10))
                    .await(PayloadRes::class.java)
            } catch (error: RuntimeException) {
                last = error
                Thread.sleep(100)
            }
        }
        throw IllegalStateException("Timed out waiting for payload endpoint.", last)
    }

    private inline fun <reified T> HttpExchange.readJson(): T =
        requestBody.use { mapper.readValue(it) }

    private fun HttpExchange.writeJson(value: Any) {
        try {
            val bytes = mapper.writeValueAsBytes(value)
            responseHeaders.add("content-type", "application/json")
            sendResponseHeaders(200, bytes.size.toLong())
            responseBody.use { it.write(bytes) }
        } catch (error: Exception) {
            val bytes = mapper.writeValueAsBytes(mapOf("error" to (error.message ?: error.javaClass.name)))
            sendResponseHeaders(500, bytes.size.toLong())
            responseBody.use { it.write(bytes) }
        }
    }

    private fun rootName(error: Throwable): String {
        var current = error
        while (current.cause != null) {
            current = current.cause!!
        }
        return current.javaClass.simpleName
    }
}
