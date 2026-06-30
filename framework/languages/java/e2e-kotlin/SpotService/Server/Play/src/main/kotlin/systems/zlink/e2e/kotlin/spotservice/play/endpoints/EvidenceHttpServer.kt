package systems.zlink.e2e.kotlin.spotservice.play.endpoints

import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.play.spots.TimerScenarioSpot
import systems.zlink.e2e.kotlin.spotservice.play.spots.MismatchedSpot
import systems.zlink.e2e.kotlin.spotservice.play.spots.UserSpot
import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.net.URLDecoder
import java.nio.charset.StandardCharsets
import java.time.Duration
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import org.springframework.context.SmartLifecycle
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spots.ZLinkSpotManager

class EvidenceHttpServer(
    private val state: ScenarioState,
    private val json: ObjectMapper,
    private val endpoint: String,
    private val spots: ZLinkSpotManager,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        if (endpoint.isBlank()) {
            return
        }
        try {
            val uri = URI.create(endpoint)
            val nextServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
            nextServer.createContext("/health") { exchange ->
                write(exchange, 200, "ok\n")
            }
            nextServer.createContext("/evidence") { exchange ->
                val body = json.writeValueAsBytes(state.snapshot())
                exchange.responseHeaders.add("Content-Type", "application/json")
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.write(body)
                exchange.close()
            }
            nextServer.createContext("/evidence/wait") { exchange ->
                requirePost(exchange)
                val request = exchange.requestBody.use { body ->
                    json.readValue(body, Contracts.EvidenceWaitRequest::class.java)
                }
                val snapshot = state.waitUntilContainsAll(
                    request.containsAll,
                    Duration.ofMillis(request.timeoutMilliseconds.coerceIn(1, 30_000).toLong())
                )
                val body = json.writeValueAsBytes(snapshot)
                exchange.responseHeaders.add("Content-Type", "application/json")
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.write(body)
                exchange.close()
            }
            nextServer.createContext("/admin/close") { exchange ->
                val rid = queryValue(exchange.requestURI, "rid")
                if (rid.isNullOrBlank()) {
                    write(exchange, 400, "missing rid\n")
                    return@createContext
                }
                val closed = try {
                    spots.close(RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, TimeUnit.SECONDS)
                } catch (error: InterruptedException) {
                    Thread.currentThread().interrupt()
                    throw IllegalStateException("spot close interrupted", error)
                } catch (error: ExecutionException) {
                    throw IllegalStateException("spot close failed", error)
                } catch (error: TimeoutException) {
                    throw IllegalStateException("spot close failed", error)
                }
                write(exchange, 200, "{\"closed\":$closed}\n")
            }
            nextServer.createContext("/admin/create-timer") { exchange ->
                val rid = queryValue(exchange.requestURI, "rid")
                if (rid.isNullOrBlank()) {
                    write(exchange, 400, "missing rid\n")
                    return@createContext
                }
                try {
                    spots.getOrCreate(TimerScenarioSpot::class.java, RoutingId.from(rid), "e2e")
                        .toCompletableFuture()
                        .get(5, TimeUnit.SECONDS)
                } catch (error: InterruptedException) {
                    Thread.currentThread().interrupt()
                    throw IllegalStateException("timer spot create interrupted", error)
                } catch (error: ExecutionException) {
                    throw IllegalStateException("timer spot create failed", error)
                } catch (error: TimeoutException) {
                    throw IllegalStateException("timer spot create failed", error)
                }
                write(exchange, 200, "{\"created\":true}\n")
            }
            nextServer.createContext("/admin/create-user") { exchange ->
                val rid = queryValue(exchange.requestURI, "rid")
                if (rid.isNullOrBlank()) {
                    write(exchange, 400, "missing rid\n")
                    return@createContext
                }
                try {
                    spots.getOrCreate(UserSpot::class.java, RoutingId.from(rid), "admin")
                        .toCompletableFuture()
                        .get(5, TimeUnit.SECONDS)
                } catch (error: InterruptedException) {
                    Thread.currentThread().interrupt()
                    throw IllegalStateException("user spot create interrupted", error)
                } catch (error: ExecutionException) {
                    throw IllegalStateException("user spot create failed", error)
                } catch (error: TimeoutException) {
                    throw IllegalStateException("user spot create failed", error)
                }
                write(exchange, 200, "{\"created\":true}\n")
            }
            nextServer.createContext("/admin/create-alternate") { exchange ->
                val rid = queryValue(exchange.requestURI, "rid")
                if (rid.isNullOrBlank()) {
                    write(exchange, 400, "missing rid\n")
                    return@createContext
                }
                try {
                    spots.getOrCreate(MismatchedSpot::class.java, RoutingId.from(rid), "admin")
                        .toCompletableFuture()
                        .get(5, TimeUnit.SECONDS)
                } catch (error: InterruptedException) {
                    Thread.currentThread().interrupt()
                    throw IllegalStateException("alternate spot create interrupted", error)
                } catch (error: ExecutionException) {
                    throw IllegalStateException("alternate spot create failed", error)
                } catch (error: TimeoutException) {
                    throw IllegalStateException("alternate spot create failed", error)
                }
                write(exchange, 200, "{\"created\":true}\n")
            }
            nextServer.createContext("/admin/type-mismatch") { exchange ->
                val rid = queryValue(exchange.requestURI, "rid")
                if (rid.isNullOrBlank()) {
                    write(exchange, 400, "missing rid\n")
                    return@createContext
                }
                try {
                    spots.getOrCreate(MismatchedSpot::class.java, RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, TimeUnit.SECONDS)
                    write(exchange, 500, "{\"mismatch\":false}\n")
                    return@createContext
                } catch (error: InterruptedException) {
                    Thread.currentThread().interrupt()
                    throw IllegalStateException("spot type mismatch interrupted", error)
                } catch (error: ExecutionException) {
                    state.record("SpotTypeMismatch", rid, error.cause?.message)
                } catch (error: TimeoutException) {
                    throw IllegalStateException("spot type mismatch timed out", error)
                } catch (error: RuntimeException) {
                    state.record("SpotTypeMismatch", rid, error.message)
                }
                try {
                    spots.getOrCreate(UserSpot::class.java, RoutingId.from(rid))
                        .toCompletableFuture()
                        .get(5, TimeUnit.SECONDS)
                    state.record("SpotTypeMismatchStateOk", rid, "existing")
                } catch (error: InterruptedException) {
                    Thread.currentThread().interrupt()
                    throw IllegalStateException("spot type mismatch follow-up interrupted", error)
                } catch (error: ExecutionException) {
                    throw IllegalStateException("spot type mismatch follow-up failed", error)
                } catch (error: TimeoutException) {
                    throw IllegalStateException("spot type mismatch follow-up failed", error)
                }
                write(exchange, 200, "{\"mismatch\":true}\n")
            }
            nextServer.start()
            server = nextServer
            running = true
        } catch (error: Exception) {
            throw IllegalStateException("failed to start evidence endpoint $endpoint", error)
        }
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean =
        running

    private fun queryValue(uri: URI, name: String): String? {
        val query = uri.rawQuery
        if (query.isNullOrBlank()) {
            return null
        }
        for (part in query.split("&")) {
            val pair = part.split("=", limit = 2)
            if (pair.size == 2 && name == pair[0]) {
                return URLDecoder.decode(pair[1], StandardCharsets.UTF_8)
            }
        }
        return null
    }

    private fun write(exchange: HttpExchange, status: Int, value: String) {
        val body = value.toByteArray(StandardCharsets.UTF_8)
        exchange.sendResponseHeaders(status, body.size.toLong())
        exchange.responseBody.write(body)
        exchange.close()
    }

    private fun requirePost(exchange: HttpExchange) {
        if (exchange.requestMethod == "POST") {
            return
        }
        write(exchange, 405, "method not allowed\n")
        throw IllegalStateException("HTTP ${exchange.requestMethod} is not allowed for ${exchange.requestURI.path}")
    }
}
