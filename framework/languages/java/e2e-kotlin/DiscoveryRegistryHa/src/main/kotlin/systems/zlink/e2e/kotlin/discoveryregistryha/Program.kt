package systems.zlink.e2e.kotlin.discoveryregistryha

import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.io.IOException
import java.net.InetSocketAddress
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.charset.StandardCharsets
import java.time.Duration
import java.util.concurrent.CompletionStage
import java.util.concurrent.TimeUnit
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.registry.ZLinkRegistryQuery
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private const val CHANNEL = "discovery.registry.ha.api"
private const val HANDLER_GROUP = "discovery-registry-ha"

fun main(args: Array<String>) {
    when (env("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "provider" -> ProviderApplication.run(*args)
        "client" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${env("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}

private fun env(name: String, fallback: String = ""): String =
    System.getenv(name)?.takeUnless { it.isBlank() } ?: fallback

private fun csv(name: String): List<String> =
    env(name).split(",").map { it.trim() }.filter { it.isNotEmpty() }

data class WorkRequest(val value: String)
data class WorkReply(val value: String, val providerRid: String)
data class MemberPeerDto(
    val channelName: String,
    val serviceRole: String,
    val endpoint: String,
    val routingId: String,
    val weight: Int,
)

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.registry"],
)
class RegistryApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(RegistryApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions =
        ZLinkEmbeddedRegistryOptions().also {
            it.setRegistryId(env("ZLINK_KOTLIN_E2E_REGISTRY_ID", "1").toInt())
            it.setPubEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_PUB"))
            it.setRouterEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            for (peer in csv("ZLINK_KOTLIN_E2E_REGISTRY_PEERS")) {
                it.addPeer(peer)
            }
        }

    @Bean
    fun registryProbeServer(query: ZLinkRegistryQuery, json: ObjectMapper): RegistryProbeServer =
        RegistryProbeServer(query, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.handlers"],
)
class ProviderApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ProviderApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun providerState(): ProviderState =
        ProviderState(env("ZLINK_KOTLIN_E2E_PROVIDER_RID", "api-a"))

    @Bean
    fun providerFramework(state: ProviderState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/${state.providerRid}-flow.log")
                .traceNodeId("kotlin-dr-${state.providerRid}")
            options.addHandlersFromPackageOf(WorkRequestHandler::class.java)
            for (registry in csv("ZLINK_KOTLIN_E2E_REGISTRY_ROUTERS")) {
                options.useDiscovery().addRegistryEndpoint(registry)
            }
            options.addClientServerChannel(CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .serverRoutingId(RoutingId.from(state.providerRid))
                .addHandlerGroup(HANDLER_GROUP)
        }

    @Bean
    fun workRequestHandler(state: ProviderState): WorkRequestHandler =
        WorkRequestHandler(state)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.client"],
)
class ClientApplication {
    companion object {
        fun run(vararg args: String) {
            val context: ConfigurableApplicationContext =
                SpringApplicationBuilder(ClientApplication::class.java)
                    .web(WebApplicationType.NONE)
                    .run(*args)
            try {
                context.getBean(ClientScenario::class.java).run()
                println("discovery-registry-ha kotlin e2e result=passed")
            } finally {
                context.close()
            }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            val scenario = env("ZLINK_KOTLIN_E2E_SCENARIO")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-$scenario-flow.log")
                .traceNodeId("kotlin-dr-client")
            for (registry in csv("ZLINK_KOTLIN_E2E_REGISTRY_ROUTERS")) {
                options.useDiscovery().addRegistryEndpoint(registry)
            }
            options.addClientServerChannel(CHANNEL).enableClient()
        }

    @Bean
    fun clientScenario(client: ZLinkClient, json: ObjectMapper): ClientScenario =
        ClientScenario(client, json)
}

class ProviderState(val providerRid: String)

@ZLinkHandlerGroup(HANDLER_GROUP)
class WorkRequestHandler(private val state: ProviderState) : ZLinkRequestHandler<WorkRequest, WorkReply> {
    override fun handle(request: WorkRequest, context: ZLinkRequestContext): WorkReply =
        WorkReply("work:${request.value}", state.providerRid)
}

class ClientScenario(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
) {
    private val http: HttpClient = HttpClient.newHttpClient()

    fun run() {
        val scenario = env("ZLINK_KOTLIN_E2E_SCENARIO")
        waitForExpectedMembers()
        val providers = requestUntilProviders(csv("ZLINK_KOTLIN_E2E_EXPECTED_RIDS"))
        if (scenario == "DR-C1") {
            expectHttpFailure(env("ZLINK_KOTLIN_E2E_DEAD_HTTP_ENDPOINT"))
        }
        println("scenario $scenario passed providers=$providers")
    }

    private fun waitForExpectedMembers() {
        val expected = csv("ZLINK_KOTLIN_E2E_EXPECTED_RIDS").toSet()
        for (probe in csv("ZLINK_KOTLIN_E2E_PROBE_HTTP_ENDPOINTS")) {
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15)
            while (System.nanoTime() < deadline) {
                val observed = memberRids(probe)
                if (observed.containsAll(expected)) {
                    break
                }
                Thread.sleep(250)
            }
            val finalObserved = memberRids(probe)
            ensure(
                finalObserved.containsAll(expected),
                "probe $probe missing expected members $expected: $finalObserved",
            )
        }
    }

    private fun memberRids(probe: String): Set<String> {
        return try {
            val root = json.readTree(get("$probe/member-peers"))
            val rids = mutableSetOf<String>()
            for (entry in root) {
                val rid = entry.path("routingId").asText("")
                if (rid.isNotBlank()) {
                    rids += rid
                }
            }
            rids
        } catch (_: Exception) {
            emptySet()
        }
    }

    private fun requestUntilProviders(expected: List<String>): Set<String> {
        val providers = mutableSetOf<String>()
        for (index in 0 until 120) {
            if (providers.containsAll(expected)) {
                break
            }
            val reply = client.requestToChannel(CHANNEL, WorkRequest("msg-$index"))
                .timeout(Duration.ofSeconds(3))
                .await(WorkReply::class.java)
            ensure(reply.value == "work:msg-$index", "reply payload mismatch")
            providers += reply.providerRid
        }
        ensure(providers.containsAll(expected), "messaging did not reach expected providers $expected: $providers")
        return providers
    }

    private fun expectHttpFailure(url: String) {
        if (url.isBlank()) {
            return
        }
        try {
            get("$url/health")
            throw IllegalStateException("dead registry probe unexpectedly responded: $url")
        } catch (expected: IllegalStateException) {
            if (expected.message?.startsWith("dead registry probe") == true) {
                throw expected
            }
        }
    }

    private fun get(url: String): String {
        try {
            val response = http.send(
                HttpRequest.newBuilder(URI.create(url))
                    .timeout(Duration.ofSeconds(2))
                    .GET()
                    .build(),
                HttpResponse.BodyHandlers.ofString(),
            )
            ensure(response.statusCode() in 200..299, "GET $url returned ${response.statusCode()}")
            return response.body()
        } catch (error: IOException) {
            throw IllegalStateException("GET failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("GET interrupted: $url", error)
        }
    }
}

private fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}

class RegistryProbeServer(
    private val query: ZLinkRegistryQuery,
    private val json: ObjectMapper,
    private val endpoint: String,
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
            nextServer.createContext("/health") { exchange -> write(exchange, "ok\n") }
            nextServer.createContext("/status") { exchange ->
                write(exchange, json.writeValueAsString(await(query.status())))
            }
            nextServer.createContext("/member-peers") { exchange ->
                val peers = await(query.memberPeers(CHANNEL)).map {
                    MemberPeerDto(
                        it.channelName(),
                        it.serviceRole().name,
                        it.endpoint(),
                        it.routingId().toString(),
                        it.weight(),
                    )
                }
                write(exchange, json.writeValueAsString(peers))
            }
            nextServer.createContext("/topology") { exchange ->
                write(exchange, json.writeValueAsString(await(query.topology())))
            }
            nextServer.start()
            server = nextServer
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

    override fun isRunning(): Boolean =
        running

    companion object {
        private fun <T> await(stage: CompletionStage<T>): T {
            try {
                return stage.toCompletableFuture().get(3, TimeUnit.SECONDS)
            } catch (error: InterruptedException) {
                Thread.currentThread().interrupt()
                throw IllegalStateException("registry query interrupted", error)
            } catch (error: Exception) {
                throw IllegalStateException("registry query failed", error)
            }
        }

        private fun write(exchange: HttpExchange, value: String) {
            val body = value.toByteArray(StandardCharsets.UTF_8)
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
    }
}
