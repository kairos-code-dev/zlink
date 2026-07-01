package systems.zlink.e2e.kotlin.resiliencelifecycle

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.time.Duration
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.springframework.context.SmartLifecycle
import systems.zlink.contracts.service.registry.ServiceRole
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.registry.ZLinkRegistryQueryClient

class ConsumerHttpServer(
    private val client: ZLinkClient,
    private val registry: ZLinkRegistryQueryClient?,
    private val json: ObjectMapper,
    private val endpoint: String,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var executor: java.util.concurrent.ExecutorService? = null
    private var running = false

    override fun start() {
        val uri = URI.create(endpoint)
        val httpServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        val requestExecutor = Executors.newCachedThreadPool { runnable ->
            Thread(runnable, "kotlin-rl-consumer-http").apply { isDaemon = true }
        }
        httpServer.createContext("/health") { exchange -> write(exchange, 200, "ok\n") }
        httpServer.createContext("/profile/request") { exchange ->
            val request = exchange.readJson(Contracts.WorkReq::class.java)
            val timeoutMillis = exchange.query("timeoutMillis")?.toLongOrNull() ?: 3000L
            val reply = client.requestToChannel(Contracts.CHANNEL, request)
                .timeout(Duration.ofMillis(timeoutMillis))
                .await(Contracts.WorkRes::class.java)
            exchange.writeJson(reply)
        }
        httpServer.createContext("/profile/send") { exchange ->
            val request = exchange.readJson(Contracts.WorkMsg::class.java)
            client.sendToChannel(Contracts.CHANNEL, request).await()
            exchange.writeJson(mapOf("status" to "sent"))
        }
        httpServer.createContext("/profile/unhandled") { exchange ->
            val request = exchange.readJson(Contracts.UnhandledReq::class.java)
            val reply = client.requestToChannel(Contracts.CHANNEL, request)
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkRes::class.java)
            exchange.writeJson(reply)
        }
        httpServer.createContext("/topology/wait") { exchange ->
            val request = exchange.readJson(Contracts.TopologyWaitReq::class.java)
            val matched = waitForTopology(request)
            exchange.writeJson(Contracts.TopologyWaitRes(matched))
        }
        httpServer.executor = requestExecutor
        httpServer.start()
        executor = requestExecutor
        server = httpServer
        running = true
    }

    private fun waitForTopology(request: Contracts.TopologyWaitReq): Int {
        val queryClient = registry ?: throw IllegalStateException("registry query client is not configured")
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < deadline) {
            val matches = try {
                queryClient.topology().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .asSequence()
                    .filter { it.channelName() == Contracts.CHANNEL }
                    .filter { it.serviceRole() == ServiceRole.ROUTER }
                    .filter { request.routingId == null || it.routingId().toString() == request.routingId }
                    .filter { request.endpoint == null || it.endpoint() == request.endpoint }
                    .count()
            } catch (_: Exception) {
                0
            }
            if (matches >= request.expectedRouters) {
                return matches
            }
            Thread.sleep(200)
        }
        throw IllegalStateException("registry topology did not match $request")
    }

    override fun stop() {
        server?.stop(0)
        server = null
        executor?.shutdownNow()
        executor = null
        running = false
    }

    override fun isRunning(): Boolean = running

    private fun <T> HttpExchange.readJson(type: Class<T>): T =
        requestBody.use { json.readValue(it, type) }

    private fun HttpExchange.writeJson(value: Any) {
        val body = json.writeValueAsBytes(value)
        responseHeaders.add("Content-Type", "application/json")
        sendResponseHeaders(200, body.size.toLong())
        responseBody.use { it.write(body) }
    }

    private fun HttpExchange.query(name: String): String? =
        requestURI.rawQuery
            ?.split("&")
            ?.mapNotNull {
                val index = it.indexOf('=')
                if (index < 0) null else it.substring(0, index) to it.substring(index + 1)
            }
            ?.firstOrNull { it.first == name }
            ?.second

    private fun write(exchange: HttpExchange, status: Int, value: String) {
        val body = value.toByteArray(StandardCharsets.UTF_8)
        exchange.sendResponseHeaders(status, body.size.toLong())
        exchange.responseBody.use { it.write(body) }
    }
}
