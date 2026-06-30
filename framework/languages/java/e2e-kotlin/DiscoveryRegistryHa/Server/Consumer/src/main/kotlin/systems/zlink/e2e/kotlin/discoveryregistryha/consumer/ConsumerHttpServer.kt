package systems.zlink.e2e.kotlin.discoveryregistryha.consumer

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.time.Duration
import org.springframework.context.SmartLifecycle
import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts
import systems.zlink.e2e.kotlin.discoveryregistryha.consumer.Configuration.ConsumerOptions
import systems.zlink.framework.channels.ZLinkClient

class ConsumerHttpServer(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
    private val options: ConsumerOptions,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        val uri = URI.create(options.httpEndpoint)
        val httpServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        httpServer.createContext("/health") { exchange ->
            write(exchange, """{"status":"ready","rid":"${options.rid}"}""")
        }
        httpServer.createContext("/profile/request") { exchange ->
            handleRequest(exchange, waitForRoute = false)
        }
        httpServer.createContext("/profile/request/wait") { exchange ->
            handleRequest(exchange, waitForRoute = true)
        }
        httpServer.start()
        server = httpServer
        running = true
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean = running

    private fun handleRequest(exchange: HttpExchange, waitForRoute: Boolean) {
        try {
            val request = json.readValue(exchange.requestBody, Contracts.WorkRequest::class.java)
            val reply = if (waitForRoute) requestWithRetry(request) else requestOnce(request)
            write(exchange, json.writeValueAsString(reply))
        } catch (error: Exception) {
            val message = error.message?.replace("\"", "'") ?: error.javaClass.simpleName
            write(exchange, """{"error":"$message"}""", status = 500)
        }
    }

    private fun requestWithRetry(request: Contracts.WorkRequest): Contracts.WorkReply {
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
        var last: Exception? = null
        while (System.nanoTime() < deadline) {
            try {
                return requestOnce(request)
            } catch (error: Exception) {
                last = error
                Thread.sleep(100)
            }
        }
        throw IllegalStateException("Timed out waiting for profile request routing.", last)
    }

    private fun requestOnce(request: Contracts.WorkRequest): Contracts.WorkReply =
        client.requestToChannel(Contracts.CHANNEL, request)
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.WorkReply::class.java)

    private fun write(exchange: HttpExchange, value: String, status: Int = 200) {
        val body = value.toByteArray(StandardCharsets.UTF_8)
        exchange.responseHeaders.add("Content-Type", "application/json")
        exchange.sendResponseHeaders(status, body.size.toLong())
        exchange.responseBody.use { stream -> stream.write(body) }
        exchange.close()
    }
}
