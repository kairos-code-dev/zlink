package systems.zlink.e2e.kotlin.registrymessaging.provider.Endpoints

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.time.Duration
import java.util.concurrent.Executors
import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.registrymessaging.provider.Configuration.ServerOptions
import systems.zlink.e2e.kotlin.registrymessaging.provider.Infrastructure.EvidenceStore
import systems.zlink.e2e.kotlin.registrymessaging.shared.Contracts
import systems.zlink.e2e.kotlin.registrymessaging.shared.EvidenceWaitReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileMsg
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.RouteMissingRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.ScenarioRoutePingReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ScenarioRoutePingRes
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRouteClient

class ProviderEndpoints(
    private val options: ServerOptions,
    private val context: ConfigurableApplicationContext,
) {
    private val mapper = jacksonObjectMapper()
    private val evidence = context.getBean(EvidenceStore::class.java)
    private val channels = context.getBean(ZLinkClient::class.java)
    private val routes = context.getBean(ZLinkRouteClient::class.java)

    fun start(): HttpServer {
        val uri = URI.create(options.httpUrl)
        val server = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        server.executor = Executors.newCachedThreadPool()
        server.createContext("/health") { exchange ->
            exchange.writeJson(mapOf("status" to "ready", "role" to "provider", "rid" to options.rid))
        }
        server.createContext("/evidence") { exchange -> exchange.writeJson(evidence.snapshot()) }
        server.createContext("/evidence/clear") { exchange ->
            evidence.clear()
            exchange.writeJson(mapOf("status" to "cleared"))
        }
        server.createContext("/evidence/wait") { exchange ->
            val request = exchange.readJson<EvidenceWaitReq>()
            exchange.writeJson(
                evidence.waitUntil(
                    request.contains,
                    Duration.ofMillis(request.timeoutMilliseconds.coerceIn(1, 30000).toLong()),
                ),
            )
        }
        server.createContext("/profile/request") { exchange ->
            val request = exchange.readJson<ProfileReq>()
            exchange.writeJson(requestProfile(Contracts.PROFILE_CHANNEL, request, Duration.ofSeconds(5)))
        }
        server.createContext("/profile/manual") { exchange ->
            val request = exchange.readJson<ProfileReq>()
            exchange.writeJson(requestProfile(Contracts.PROFILE_MANUAL_CHANNEL, request, Duration.ofSeconds(5)))
        }
        server.createContext("/profile/command") { exchange ->
            val command = exchange.readJson<ProfileMsg>()
            sendProfile(Contracts.PROFILE_CHANNEL, command, Contracts.PROFILE_COMMAND_PACKET)
            exchange.writeJson(mapOf("status" to "sent"))
        }
        server.createContext("/profile/route/request") { exchange ->
            val request = exchange.readJson<ScenarioRoutePingReq>()
            exchange.writeJson(requestRoute(RoutingId.from("api-b"), request))
        }
        server.createContext("/profile/route/missing") { exchange ->
            val request = exchange.readJson<ScenarioRoutePingReq>()
            var failed = false
            try {
                routes.requestTo(Contracts.PROFILE_ROUTE_CHANNEL, RoutingId.from("missing-rid"), request)
                    .packetName(Contracts.ROUTE_PACKET)
                    .timeout(Duration.ofMillis(300))
                    .await(ScenarioRoutePingRes::class.java)
            } catch (_: RuntimeException) {
                failed = true
            }
            exchange.writeJson(RouteMissingRes(failed))
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

    private fun requestProfile(channelName: String, request: ProfileReq, timeout: Duration): ProfileRes {
        val deadline = System.nanoTime() + Duration.ofSeconds(30).toNanos()
        var last: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                return channels.requestToChannel(channelName, request)
                    .packetName(Contracts.PROFILE_REQUEST_PACKET)
                    .timeout(timeout)
                    .await(ProfileRes::class.java)
            } catch (error: RuntimeException) {
                last = error
                Thread.sleep(100)
            }
        }
        throw IllegalStateException("Timed out waiting for profile request channel route.", last)
    }

    private fun sendProfile(channelName: String, command: ProfileMsg, packetName: String) {
        channels.sendToChannel(channelName, command)
            .packetName(packetName)
            .submit()
    }

    private fun requestRoute(target: RoutingId, request: ScenarioRoutePingReq): ScenarioRoutePingRes {
        val deadline = System.nanoTime() + Duration.ofSeconds(30).toNanos()
        var last: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                return routes.requestTo(Contracts.PROFILE_ROUTE_CHANNEL, target, request)
                    .packetName(Contracts.ROUTE_PACKET)
                    .timeout(Duration.ofSeconds(5))
                    .await(ScenarioRoutePingRes::class.java)
            } catch (error: RuntimeException) {
                last = error
                Thread.sleep(100)
            }
        }
        throw IllegalStateException("Timed out waiting for route mesh target.", last)
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
}
