package systems.zlink.e2e.kotlin.discoveryregistryha.probe

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
import systems.zlink.e2e.kotlin.discoveryregistryha.probe.Configuration.ProbeOptions
import systems.zlink.framework.registry.ZLinkRegistryQueryClient

class ProbeHttpServer(
    private val query: ZLinkRegistryQueryClient,
    private val json: ObjectMapper,
    private val options: ProbeOptions,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        val uri = URI.create(options.httpEndpoint)
        val httpServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        httpServer.createContext("/health") { exchange ->
            write(exchange, """{"status":"ready","registryRouter":"${options.registryRouter}"}""")
        }
        httpServer.createContext("/registry/topology") { exchange ->
            val topology = await(query.topology())
                .filter { entry -> Contracts.CHANNEL == entry.channelName() }
            write(exchange, json.writeValueAsString(topology))
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
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean = running

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
