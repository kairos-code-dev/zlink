package systems.zlink.e2e.kotlin.discoveryregistryha.registry

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.util.concurrent.CompletionStage
import java.util.concurrent.TimeUnit
import org.springframework.context.SmartLifecycle
import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts
import systems.zlink.framework.registry.ZLinkRegistryQuery

class RegistryProbeServer(
    private val query: ZLinkRegistryQuery,
    private val json: ObjectMapper,
    private val endpoint: String?,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        if (endpoint.isNullOrBlank()) {
            return
        }
        try {
            val uri = URI.create(endpoint)
            val httpServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
            httpServer.createContext("/health") { exchange -> write(exchange, "ok\n") }
            httpServer.createContext("/status") { exchange ->
                write(exchange, json.writeValueAsString(await(query.status())))
            }
            httpServer.createContext("/member-peers") { exchange ->
                val members = await(query.memberPeers(Contracts.CHANNEL))
                    .map { peer ->
                        MemberPeerDto(
                            peer.channelName(),
                            peer.serviceRole().name,
                            peer.endpoint(),
                            peer.routingId().toString(),
                            peer.weight(),
                        )
                    }
                write(exchange, json.writeValueAsString(members))
            }
            httpServer.createContext("/topology") { exchange ->
                write(exchange, json.writeValueAsString(await(query.topology())))
            }
            httpServer.createContext("/topology-rids") { exchange ->
                val rids = await(query.topology())
                    .filter { entry -> Contracts.CHANNEL == entry.channelName() }
                    .map { entry -> entry.routingId().toString() }
                    .sorted()
                write(exchange, json.writeValueAsString(rids))
            }
            httpServer.start()
            server = httpServer
            running = true
        } catch (error: Exception) {
            throw IllegalStateException("failed to start registry probe $endpoint", error)
        }
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean = running

    private data class MemberPeerDto(
        val channelName: String,
        val serviceRole: String,
        val endpoint: String,
        val routingId: String,
        val weight: Int,
    )

    private companion object {
        fun <T> await(stage: CompletionStage<T>): T =
            try {
                stage.toCompletableFuture().get(3, TimeUnit.SECONDS)
            } catch (error: InterruptedException) {
                Thread.currentThread().interrupt()
                throw IllegalStateException("registry query interrupted", error)
            } catch (error: Exception) {
                throw IllegalStateException("registry query failed", error)
            }

        fun write(exchange: HttpExchange, value: String) {
            val body = value.toByteArray(StandardCharsets.UTF_8)
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { stream -> stream.write(body) }
            exchange.close()
        }
    }
}
