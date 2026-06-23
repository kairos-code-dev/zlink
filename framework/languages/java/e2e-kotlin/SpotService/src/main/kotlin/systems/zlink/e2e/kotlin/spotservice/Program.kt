package systems.zlink.e2e.kotlin.spotservice

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import java.util.concurrent.TimeUnit
import java.util.function.Supplier
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkSpotKind
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.ZLinkSpotOutbound
import systems.zlink.framework.spots.ZLinkSpotPacketHandler
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver
import systems.zlink.framework.spots.ZLinkSpotRequestHandler
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private const val ROUTE_CHANNEL = "spot.service.route"
private const val EGRESS_CHANNEL = "spot.service.egress"
private const val INGRESS_CHANNEL = "spot.service.ingress"
private const val SPOT_MESH = "spot.service.mesh"
private const val SPOT_NODE = "play"

fun main(args: Array<String>) {
    when (env("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "play" -> PlayApplication.run(*args)
        "client" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${env("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}

private fun env(name: String, fallback: String = ""): String =
    System.getenv(name)?.takeUnless { it.isBlank() } ?: fallback

data class StateRequest(val op: String)
data class StateReply(val spotRid: String, val nodeRid: String, val value: String)
data class StateCommand(val value: String)
data class SlowRequest(val value: String)
data class EvidenceEntry(val marker: String, val nodeRid: String, val spotRid: String, val value: String)
data class EvidenceSnapshot(val nodeRid: String, val entries: List<EvidenceEntry>)

class ScenarioState(val nodeRid: String) {
    private val entries = mutableListOf<EvidenceEntry>()

    @Synchronized
    fun record(marker: String, spotRid: String, value: String) {
        entries += EvidenceEntry(marker, nodeRid, spotRid, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(nodeRid, entries.toList())
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.registry"],
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
    fun registryOptions(): ZLinkEmbeddedRegistryOptions =
        ZLinkEmbeddedRegistryOptions().also {
            it.setPubEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_PUB"))
            it.setRouterEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
        }
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.play"],
)
class PlayApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(PlayApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState(env("ZLINK_KOTLIN_E2E_NODE_RID", "play-a"))

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun evidenceHttpServer(state: ScenarioState, json: ObjectMapper): EvidenceHttpServer =
        EvidenceHttpServer(state, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun playFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val nodeRid = state.nodeRid
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.addSpotRemoteAddressResolver(SpotRouteResolver::class.java)
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/$nodeRid-flow.log")
                .traceNodeId("kotlin-sm-$nodeRid")
                .setMessageDispatchErrorObserver { error ->
                    state.record(
                        "DispatchError",
                        error.spotRid(),
                        "${error.reason()}/${error.action()}/${error.packetName()}",
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addRouteMeshChannel(ROUTE_CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT"))
                .enableClient(env("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(env("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .enableSpotRouteEgress(ROUTE_CHANNEL)
                .setRoutingId(RoutingId.from(nodeRid))
            options.addClientServerChannel(INGRESS_CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_INGRESS_ENDPOINT"))
                .serverRoutingId(RoutingId.from(nodeRid))
                .addRequestHandler(NoopIngressHandler::class.java, String::class.java, String::class.java, "Noop")
            val node: ZLinkSpotNodeBuilder = options.addSpotMesh(SPOT_MESH)
                .addNode(SPOT_NODE)
            node.enableRouter(env("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRouterRoutingId(RoutingId.from(nodeRid))
            node.acceptSpotRoutesFromChannel(ROUTE_CHANNEL)
            node.acceptSpotRoutesFromChannel(INGRESS_CHANNEL, env("ZLINK_KOTLIN_E2E_INGRESS_ENDPOINT"))
            node.addSpotFactory(UserSpot::class.java)
        }

    @Bean
    fun createOwnedSpot(spots: ZLinkSpotManager, state: ScenarioState): ApplicationRunner =
        ApplicationRunner {
            val spotRid = if (state.nodeRid == "play-a") "room-a" else "room-b"
            spots.getOrCreate(UserSpot::class.java, RoutingId.from(spotRid), "bootstrap")
                .toCompletableFuture()
                .join()
        }
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.client"],
)
class ClientApplication {
    companion object {
        fun run(vararg args: String) {
            val context: ConfigurableApplicationContext =
                SpringApplicationBuilder(ClientApplication::class.java)
                    .web(WebApplicationType.NONE)
                    .run(*args)
            try {
                val spots = context.getBean(ZLinkSpotManager::class.java)
                val mode = env("ZLINK_KOTLIN_E2E_CLIENT_MODE", "state1")
                ClientDriverSpot.configure(mode)
                spots.create(ClientDriverSpot::class.java, RoutingId.from("client-driver-$mode"))
                    .toCompletableFuture()
                    .join()
                ClientDriverSpot.awaitResult()
                println("spot-service kotlin e2e mode=$mode result=passed")
            } finally {
                context.close()
            }
        }
    }

    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState("client")

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.addSpotRemoteAddressResolver(SpotRouteResolver::class.java)
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceNodeId("kotlin-sm-client")
            options.addRouteMeshChannel(ROUTE_CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT"))
                .enableClient(env("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(env("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from("client"))
            options.addClientServerChannel(EGRESS_CHANNEL)
                .enableClient(env("ZLINK_KOTLIN_E2E_INGRESS_A_ENDPOINT"))
                .enableSpotRouteEgress(INGRESS_CHANNEL)
            val node = options.addSpotMesh(SPOT_MESH)
                .addNode("client")
            node.enableRouter(env("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRouterRoutingId(RoutingId.from("client"))
            node.acceptSpotRoutesFromChannel(ROUTE_CHANNEL)
            node.addSpotFactory(ClientDriverSpot::class.java)
        }
}

class UserSpot(
    private val context: ZLinkSpotContext,
    private val evidence: ScenarioState,
) : ZLinkSpot<ZLinkActor> {
    private var state = ""

    override fun context(): ZLinkSpotContext =
        context

    override fun configure() {
        context.handlers().addPacket(StateRequestHandler::class.java)
        context.handlers().addPacket(StateCommandHandler::class.java)
        context.handlers().addPacket(SlowRequestHandler::class.java)
    }

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse {
        evidence.record("SpotCreated", context.spotRid().toString(), if (request.isEmpty) "" else "request")
        return ZLinkSpotCreateResponse.accept()
    }

    override fun onInitialize() {
        evidence.record("SpotInitialized", context.spotRid().toString(), "")
    }

    override fun onClosing() {
        evidence.record("SpotClosing", context.spotRid().toString(), state)
    }

    fun apply(op: String): String {
        state = if (state.isBlank()) op else "$state,$op"
        evidence.record("StateRequest", context.spotRid().toString(), state)
        return state
    }

    fun command(value: String) {
        evidence.record("StateCommand", context.spotRid().toString(), value)
    }
}

class StateRequestHandler : ZLinkSpotRequestHandler<UserSpot, StateRequest, StateReply> {
    override fun handle(spot: UserSpot, request: StateRequest): StateReply =
        StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            spot.apply(request.op),
        )
}

class StateCommandHandler : ZLinkSpotPacketHandler<UserSpot, StateCommand> {
    override fun handle(spot: UserSpot, message: StateCommand) {
        spot.command(message.value)
    }
}

class SlowRequestHandler : ZLinkSpotRequestHandler<UserSpot, SlowRequest, StateReply> {
    override fun handle(spot: UserSpot, request: SlowRequest): StateReply {
        Thread.sleep(1000)
        return StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            spot.apply("slow:${request.value}"),
        )
    }
}

class ClientDriverSpot(
    private val context: ZLinkSpotContext,
) : ZLinkSpot<ZLinkActor> {
    companion object {
        private var result: CompletableFuture<Void> = CompletableFuture()
        private var mode: String = "state1"

        fun configure(nextMode: String) {
            mode = nextMode
            result = CompletableFuture()
        }

        fun awaitResult() {
            try {
                result.get(45, TimeUnit.SECONDS)
            } catch (error: Exception) {
                throw IllegalStateException("client driver scenario failed", error)
            }
        }
    }

    override fun context(): ZLinkSpotContext =
        context

    override fun onInitialize() {
        try {
            ClientScenario(context.outbound()).runMode(mode)
            result.complete(null)
        } catch (error: Throwable) {
            result.completeExceptionally(error)
        }
    }
}

class ClientScenario(
    private val outbound: ZLinkSpotOutbound,
) {
    fun runMode(mode: String) {
        when (mode) {
            "state1" -> runState1()
            "state2" -> runState2()
            "send" -> runSend()
            "timeout" -> runTimeout()
            "missing" -> runMissingPacket()
            "normal" -> runNormal()
            else -> throw IllegalArgumentException("unknown client mode $mode")
        }
    }

    private fun runState1() {
        val first = eventually {
            outbound.requestToSpot(RoutingId.from("room-a"), StateRequest("a1"))
                .timeout(REQUEST_TIMEOUT)
                .await(StateReply::class.java)
        }
        ensure(first.spotRid == "room-a", "SM-A1 wrong spot rid")
        ensure(first.nodeRid == "play-a", "SM-A1 wrong owner node")
        println("scenario SM-A1 passed")
    }

    private fun runState2() {
        val second = eventually {
            outbound.requestToSpot(RoutingId.from("room-a"), StateRequest("a2"))
                .timeout(REQUEST_TIMEOUT)
                .await(StateReply::class.java)
        }
        ensure(second.value.contains("a1") && second.value.contains("a2"), "SM-A2 state did not accumulate")
        println("scenario SM-A2 passed")
    }

    private fun runSend() {
        outbound.sendToSpot(RoutingId.from("room-a"), StateCommand("cmd-c1"))
            .await()
        println("scenario SM-C1-send passed")
    }

    private fun runTimeout() {
        expectFailure {
            outbound.requestToSpot(RoutingId.from("room-a"), SlowRequest("late"))
                .timeout(Duration.ofMillis(100))
                .await(StateReply::class.java)
        }
        println("scenario SM-C1-timeout passed")
    }

    private fun runNormal() {
        val after = eventually {
            outbound.requestToSpot(RoutingId.from("room-a"), StateRequest("after-timeout"))
                .timeout(REQUEST_TIMEOUT)
                .await(StateReply::class.java)
        }
        ensure(after.value.contains("after-timeout"), "SM-C1 post-timeout request failed")
        println("scenario SM-C1-normal passed")
    }

    private fun runMissingPacket() {
        expectFailure {
            outbound.requestToSpot(RoutingId.from("room-a"), StateRequest("missing"))
                .packetName("MissingSpotPacket")
                .timeout(REQUEST_TIMEOUT)
                .await(StateReply::class.java)
        }
        outbound.sendToSpot(RoutingId.from("room-a"), StateCommand("missing-send"))
            .packetName("MissingSpotCommand")
            .await()
        println("scenario SM-C1-negative passed")
    }

    private fun expectFailure(action: () -> Unit) {
        try {
            action()
        } catch (error: RuntimeException) {
            return
        }
        throw IllegalStateException("operation unexpectedly succeeded")
    }

    private fun <T> eventually(action: Supplier<T>): T {
        val deadline = System.nanoTime() + EVENTUAL_TIMEOUT.toNanos()
        var lastFailure: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                return action.get()
            } catch (error: RuntimeException) {
                lastFailure = error
                Thread.sleep(200)
            }
        }
        throw IllegalStateException("operation did not succeed before timeout", lastFailure)
    }

    private fun ensure(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException(message)
        }
    }

    companion object {
        private val REQUEST_TIMEOUT: Duration = Duration.ofSeconds(5)
        private val EVENTUAL_TIMEOUT: Duration = Duration.ofSeconds(30)
    }
}

class SpotRouteResolver : ZLinkSpotRemoteAddressResolver {
    override fun resolveSpotRemoteAddressAsync(spotRid: RoutingId): CompletionStage<ZLinkSpotRemoteAddress> {
        val targetNode = if (spotRid.toString().contains("b")) "play-b" else "play-a"
        return CompletableFuture.completedFuture(
            ZLinkSpotRemoteAddress(
                INGRESS_CHANNEL,
                RoutingId.from(targetNode),
                spotRid,
                ZLinkSpotKind.USER,
            ),
        )
    }
}

class NoopIngressHandler : ZLinkRequestHandler<String, String> {
    override fun handle(request: String, context: ZLinkRequestContext): String =
        request
}

class EvidenceHttpServer(
    private val state: ScenarioState,
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
            nextServer.createContext("/health") { exchange ->
                val body = "ok\n".toByteArray(StandardCharsets.UTF_8)
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.use { it.write(body) }
            }
            nextServer.createContext("/evidence") { exchange ->
                val body = json.writeValueAsBytes(state.snapshot())
                exchange.responseHeaders.add("Content-Type", "application/json")
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.use { it.write(body) }
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
}
