package systems.zlink.e2e.kotlin.monitoring

import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import com.sun.net.httpserver.HttpServer
import java.net.HttpURLConnection
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.time.Duration
import org.springframework.beans.factory.ObjectProvider
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.monitoring.ZLinkRegistryEvent
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler
import systems.zlink.framework.monitoring.ZLinkSocketEvent
import systems.zlink.framework.monitoring.ZLinkSpotEvent
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.spring.ZLinkMonitoringLifecycle
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer

private const val CHANNEL = "monitoring.api"
private const val SPOT_MESH = "monitoring.spot.mesh"
private const val SPOT_NODE = "monitoring.spot.node"
private const val HANDLER_GROUP = "monitoring"
private const val REGISTRY_SOURCE = "ops-registry"

fun main(args: Array<String>) {
    when (env("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "service" -> ServiceApplication.run(*args)
        "client" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${env("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}

private fun env(name: String, fallback: String = ""): String =
    System.getenv(name)?.takeUnless { it.isBlank() } ?: fallback

data class WorkRequest(val value: String)
data class WorkReply(val value: String, val providerRid: String)
data class EvidenceEntry(val surface: String, val sourceName: String, val event: String, val detail: String)
data class EvidenceSnapshot(val entries: List<EvidenceEntry>)

class EvidenceState {
    private val entries = mutableListOf<EvidenceEntry>()

    @Synchronized
    fun record(surface: String, sourceName: String, event: String, detail: String) {
        entries += EvidenceEntry(surface, sourceName, event, detail)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(entries.toList())
}

class EvidenceHttpServer(
    private val state: EvidenceState,
    private val json: ObjectMapper,
    private val endpoint: String,
) : SmartLifecycle {
    private var server: HttpServer? = null
    @Volatile
    private var running = false

    override fun start() {
        if (endpoint.isBlank()) {
            return
        }
        try {
            val uri = URI.create(endpoint)
            val nextServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
            nextServer.createContext("/health") { exchange -> write(exchange, "ok\n") }
            nextServer.createContext("/evidence") { exchange ->
                write(exchange, json.writeValueAsString(state.snapshot()))
            }
            nextServer.start()
            server = nextServer
            running = true
        } catch (error: Exception) {
            throw IllegalStateException("failed to start evidence endpoint $endpoint", error)
        }
    }

    private fun write(exchange: com.sun.net.httpserver.HttpExchange, value: String) {
        val body = value.toByteArray(StandardCharsets.UTF_8)
        exchange.responseHeaders.add("Content-Type", "application/json")
        exchange.sendResponseHeaders(200, body.size.toLong())
        exchange.responseBody.write(body)
        exchange.close()
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean =
        running
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.monitoring.registry"],
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
        ObjectMapper().registerKotlinModule()

    @Bean
    fun evidenceState(): EvidenceState =
        EvidenceState()

    @Bean
    fun evidenceHttpServer(state: EvidenceState, json: ObjectMapper): EvidenceHttpServer =
        EvidenceHttpServer(state, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions =
        ZLinkEmbeddedRegistryOptions().also {
            it.setPubEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_PUB"))
            it.setRouterEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
        }

    @Bean
    fun monitoringOptions(): ZLinkMonitoringOptionsCustomizer =
        ZLinkMonitoringOptionsCustomizer { options ->
            options.addRegistryEvents(REGISTRY_SOURCE, Duration.ofMillis(100))
        }

    @Bean
    fun registryRecorder(state: EvidenceState): RegistryRecorder =
        RegistryRecorder(state)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.monitoring.handlers"],
)
class ServiceApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ServiceApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().registerKotlinModule()

    @Bean
    fun evidenceState(): EvidenceState =
        EvidenceState()

    @Bean
    fun evidenceHttpServer(state: EvidenceState, json: ObjectMapper): EvidenceHttpServer =
        EvidenceHttpServer(state, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun frameworkConfigurer(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/service-flow.log")
                .traceNodeId("kotlin-mon-service")
            options.addClientServerChannel(CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .serverRoutingId(RoutingId.from("svc-a"))
                .addRequestHandler(
                    WorkRequestHandler::class.java,
                    WorkRequest::class.java,
                    WorkReply::class.java,
                    "WorkRequest",
                )
            val node: ZLinkSpotNodeBuilder = options.addSpotMesh(SPOT_MESH)
                .addNode(SPOT_NODE)
            node.enableRouter(env("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRouterRoutingId(RoutingId.from("svc-a-spot"))
            node.enablePubSub(env("ZLINK_KOTLIN_E2E_SPOT_PUB_ENDPOINT"))
                .setPubSubRoutingId(RoutingId.from("svc-a-spot-pub"))
            node.addSpotFactory(MonitoringSpot::class.java)
        }

    @Bean
    fun monitoringOptions(): ZLinkMonitoringOptionsCustomizer =
        ZLinkMonitoringOptionsCustomizer { options ->
            options.addSocketEvents(CHANNEL)
            options.addSpotEvents(SPOT_NODE, Duration.ofMillis(100))
        }

    @Bean
    fun workRequestHandler(): WorkRequestHandler =
        WorkRequestHandler()

    @Bean
    fun createSpot(spots: ZLinkSpotManager): ApplicationRunner =
        ApplicationRunner {
            spots.getOrCreate(MonitoringSpot::class.java, RoutingId.from("monitoring-room"), "bootstrap")
                .toCompletableFuture()
                .join()
        }

    @Bean
    fun recordMonitoringLifecycle(
        lifecycle: ObjectProvider<ZLinkMonitoringLifecycle>,
        state: EvidenceState,
    ): ApplicationRunner =
        ApplicationRunner {
            state.record(
                "system",
                "service",
                "MonitoringLifecycle",
                "running=${lifecycle.stream().anyMatch { it.isRunning }}",
            )
        }

    @Bean
    fun socketRecorder(state: EvidenceState): SocketRecorder =
        SocketRecorder(state)

    @Bean
    fun spotRecorder(state: EvidenceState): SpotRecorder =
        SpotRecorder(state)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.monitoring.client"],
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
                println("monitoring kotlin e2e result=passed")
            } finally {
                context.close()
            }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().registerKotlinModule()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceNodeId("kotlin-mon-client")
            options.addClientServerChannel(CHANNEL)
                .enableClient(env("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
        }

    @Bean
    fun clientScenario(client: ZLinkClient, json: ObjectMapper): ClientScenario =
        ClientScenario(client, json)
}

class ClientScenario(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
) {
    fun run() {
        for (index in 0 until 5) {
            val reply = client.requestToChannel(CHANNEL, WorkRequest("a1-$index"))
                .timeout(Duration.ofSeconds(3))
                .await(WorkReply::class.java)
            ensure(reply.value == "work:a1-$index", "reply mismatch")
        }
        waitForEvent(
            env("ZLINK_KOTLIN_E2E_REGISTRY_HTTP"),
            "registry",
            setOf("STATUS_CHANGED", "TOPOLOGY_CHANGED", "SERVICE_SUMMARY_CHANGED"),
        )
        waitForAnyEvent(
            env("ZLINK_KOTLIN_E2E_SERVICE_HTTP"),
            "socket",
            setOf("CONNECTED", "CONNECTION_READY"),
        )
        waitForEvent(
            env("ZLINK_KOTLIN_E2E_SERVICE_HTTP"),
            "spot",
            setOf("STATUS_CHANGED", "PEERS_CHANGED", "SUBJECTS_CHANGED", "TIMER_HANDLER_FAILED"),
        )
        println("scenario MON-A1 passed")
        println("scenario MON-A2 passed")
        println("scenario MON-A3 passed")
    }

    private fun waitForEvent(baseUrl: String, surface: String, expected: Set<String>) {
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < deadline) {
            val observed = events(baseUrl, surface)
            if (observed.containsAll(expected)) {
                return
            }
            Thread.sleep(200)
        }
        val observed = events(baseUrl, surface)
        throw IllegalStateException(
            "missing $surface events $expected at $baseUrl; observed=$observed; evidence=${get("$baseUrl/evidence")}",
        )
    }

    private fun waitForAnyEvent(baseUrl: String, surface: String, expected: Set<String>) {
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < deadline) {
            val observed = events(baseUrl, surface)
            if (expected.any(observed::contains)) {
                return
            }
            Thread.sleep(200)
        }
        val observed = events(baseUrl, surface)
        throw IllegalStateException(
            "missing any $surface event $expected at $baseUrl; observed=$observed; evidence=${get("$baseUrl/evidence")}",
        )
    }

    private fun events(baseUrl: String, surface: String): Set<String> =
        try {
            val root: JsonNode = json.readTree(get("$baseUrl/evidence")).path("entries")
            root.filter { entry -> surface == entry.path("surface").asText() }
                .map { entry -> entry.path("event").asText() }
                .toSet()
        } catch (_: Exception) {
            emptySet()
        }

    private fun get(url: String): String {
        val connection = URI.create(url).toURL().openConnection() as HttpURLConnection
        connection.connectTimeout = 3000
        connection.readTimeout = 3000
        connection.requestMethod = "GET"
        ensure(connection.responseCode in 200..299, "GET $url returned ${connection.responseCode}")
        return connection.inputStream.bufferedReader(StandardCharsets.UTF_8).use { it.readText() }
    }
}

class SocketRecorder(private val state: EvidenceState) : ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
    override fun handle(event: ZLinkSocketEvent) {
        state.record(
            "socket",
            event.sourceName(),
            event.event().name,
            "${event.localAddr()}|${event.remoteAddr()}|${event.routingId().map { it.toString() }.orElse("")}",
        )
    }
}

class RegistryRecorder(private val state: EvidenceState) : ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
    override fun handle(event: ZLinkRegistryEvent) {
        state.record(
            "registry",
            event.sourceName(),
            event.event().name,
            "topology=${event.topology().size}|summary=${event.serviceSummary().size}",
        )
    }
}

class SpotRecorder(private val state: EvidenceState) : ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
    override fun handle(event: ZLinkSpotEvent) {
        state.record(
            "spot",
            event.sourceName(),
            event.event().name,
            "peers=${event.peers().size}|subjects=${event.subjects().size}" +
                event.timerDiagnostic().map { value -> "|timer=$value" }.orElse(""),
        )
    }
}

class WorkRequestHandler : ZLinkRequestHandler<WorkRequest, WorkReply> {
    override fun handle(request: WorkRequest, context: ZLinkRequestContext): WorkReply =
        WorkReply("work:${request.value}", "svc-a")
}

class MonitoringSpot(private val context: ZLinkSpotContext) : ZLinkSpot<ZLinkActor> {
    override fun context(): ZLinkSpotContext =
        context

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse {
        val options = ZLinkTimerOptions()
        options.setStopOnUnhandledException(false)
        context.addTimer("failing-monitoring-timer", Duration.ofMillis(100), FailingTimerHandler::class.java, options)
        return ZLinkSpotCreateResponse.accept()
    }
}

class FailingTimerHandler : ZLinkSpotTimerHandler<MonitoringSpot> {
    override fun handle(spot: MonitoringSpot, tick: ZLinkTimerTick) {
        throw IllegalStateException("monitoring timer boom")
    }
}

private fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}
