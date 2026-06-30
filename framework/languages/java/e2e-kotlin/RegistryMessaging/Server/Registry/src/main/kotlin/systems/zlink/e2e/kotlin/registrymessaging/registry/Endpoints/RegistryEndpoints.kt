package systems.zlink.e2e.kotlin.registrymessaging.registry.Endpoints

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.util.concurrent.Executors
import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.e2e.kotlin.registrymessaging.registry.Configuration.ServerOptions
import systems.zlink.e2e.kotlin.registrymessaging.registry.Infrastructure.EvidenceStore
import systems.zlink.e2e.kotlin.registrymessaging.shared.Contracts
import systems.zlink.framework.registry.ZLinkRegistryQuery
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter

class RegistryEndpoints(
    private val options: ServerOptions,
    private val context: ConfigurableApplicationContext,
) {
    private val mapper = jacksonObjectMapper()
    private val evidence = context.getBean(EvidenceStore::class.java)
    private val query = context.getBean(ZLinkRegistryQuery::class.java)

    fun start(): HttpServer {
        val uri = URI.create(options.httpUrl)
        val server = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        server.executor = Executors.newCachedThreadPool()
        server.createContext("/health") { exchange ->
            exchange.writeJson(mapOf("status" to "ready", "role" to "registry", "rid" to options.rid))
        }
        server.createContext("/registry/topology") { exchange ->
            try {
                exchange.writeJson(
                    query.topology(ZLinkRegistryTopologyFilter.channel(Contracts.PROFILE_CHANNEL))
                        .toCompletableFuture()
                        .get()
                        .map {
                            mapOf(
                                "channelName" to it.channelName(),
                                "serviceRole" to it.serviceRole().toString(),
                                "state" to it.state().toString(),
                                "routingId" to it.routingId().toString(),
                                "endpoint" to it.endpoint(),
                            )
                        },
                )
            } catch (error: Exception) {
                exchange.writeJsonError(error)
            }
        }
        server.createContext("/evidence") { exchange -> exchange.writeJson(evidence.snapshot()) }
        server.createContext("/evidence/clear") { exchange ->
            evidence.clear()
            exchange.writeJson(mapOf("status" to "cleared"))
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

    private fun HttpExchange.writeJson(value: Any) {
        try {
            val bytes = mapper.writeValueAsBytes(value)
            responseHeaders.add("content-type", "application/json")
            sendResponseHeaders(200, bytes.size.toLong())
            responseBody.use { it.write(bytes) }
        } catch (error: Exception) {
            val bytes = mapper.writeValueAsBytes(mapOf("error" to (error.message ?: error.javaClass.name)))
            responseHeaders.add("content-type", "application/json")
            sendResponseHeaders(500, bytes.size.toLong())
            responseBody.use { it.write(bytes) }
        }
    }

    private fun HttpExchange.writeJsonError(error: Exception) {
        val bytes = mapper.writeValueAsBytes(
            mapOf("error" to (error.message ?: error.javaClass.name), "type" to error.javaClass.name),
        )
        responseHeaders.add("content-type", "application/json")
        sendResponseHeaders(500, bytes.size.toLong())
        responseBody.use { it.write(bytes) }
    }
}
