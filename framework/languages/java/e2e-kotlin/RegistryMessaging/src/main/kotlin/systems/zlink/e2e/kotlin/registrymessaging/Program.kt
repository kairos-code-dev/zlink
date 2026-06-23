package systems.zlink.e2e.kotlin.registrymessaging

import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.delay
import kotlinx.coroutines.future.await
import kotlinx.coroutines.runBlocking
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSendHandler
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private const val API_CHANNEL = "registry.messaging.api"
private const val WORKFLOW_CHANNEL = "registry.messaging.workflow"
private const val ROUTE_CHANNEL = "registry.messaging.route"
private const val HANDLER_GROUP = "registry-messaging"
private const val ROUTE_PACKET = "ScenarioRoutePing"

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

data class ProfileRequest(val value: String)
data class ProfileReply(val value: String, val providerRid: String, val instanceId: String)
data class ProfileCommand(val commandId: String)
data class RoutePing(val value: String)
data class RoutePong(val value: String, val targetRid: String, val sourceRid: String)
data class EvidenceEntry(val marker: String, val providerRid: String, val value: String)
data class EvidenceSnapshot(val providerRid: String, val entries: List<EvidenceEntry>)

class ScenarioState(
    val providerRid: String,
    val instanceId: String,
) {
    private val entries = mutableListOf<EvidenceEntry>()

    @Synchronized
    fun record(marker: String, value: String) {
        entries += EvidenceEntry(marker, providerRid, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(providerRid, entries.toList())
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrymessaging.registry"],
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
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrymessaging.provider"],
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
    fun scenarioState(): ScenarioState {
        val rid = env("ZLINK_KOTLIN_E2E_PROVIDER_RID", "api-a")
        return ScenarioState(rid, env("ZLINK_KOTLIN_E2E_PROVIDER_INSTANCE", rid))
    }

    @Bean
    fun providerFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/${state.providerRid}-flow.log")
                .traceNodeId("kotlin-rm-${state.providerRid}")
                .setMessageDispatchErrorObserver { error ->
                    state.record(
                        "DispatchError",
                        "${error.reason()}/${error.action()}/${error.packetName()}",
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addHandlersFromPackageOf(ProfileRequestHandler::class.java)
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))

            val apiEndpoint = env("ZLINK_KOTLIN_E2E_API_ENDPOINT")
            if (apiEndpoint.isNotBlank()) {
                options.addClientServerChannel(API_CHANNEL)
                    .enableServer(apiEndpoint)
                    .serverRoutingId(RoutingId.from(state.providerRid))
                    .addHandlerGroup(HANDLER_GROUP)
            }

            val workflowEndpoint = env("ZLINK_KOTLIN_E2E_WORKFLOW_ENDPOINT")
            if (workflowEndpoint.isNotBlank()) {
                options.addClientServerChannel(WORKFLOW_CHANNEL)
                    .enableServer(workflowEndpoint)
                    .serverRoutingId(RoutingId.from(state.providerRid))
                    .addHandlerGroup(HANDLER_GROUP)
            }

            val routeEndpoint = env("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT")
            if (routeEndpoint.isNotBlank()) {
                options.addRouteMeshChannel(ROUTE_CHANNEL)
                    .enableServer(routeEndpoint)
                    .setRoutingId(RoutingId.from(state.providerRid))
                    .addRequestHandler(
                        RoutePingHandler::class.java,
                        RoutePing::class.java,
                        RoutePong::class.java,
                        ROUTE_PACKET,
                    )
            }
        }

    @Bean
    fun profileRequestHandler(state: ScenarioState): ProfileRequestHandler =
        ProfileRequestHandler(state)

    @Bean
    fun profileCommandHandler(state: ScenarioState): ProfileCommandHandler =
        ProfileCommandHandler(state)

    @Bean
    fun routePingHandler(state: ScenarioState): RoutePingHandler =
        RoutePingHandler(state)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrymessaging.client"],
)
class ClientApplication {
    companion object {
        fun run(vararg args: String) {
            val context: ConfigurableApplicationContext =
                SpringApplicationBuilder(ClientApplication::class.java)
                    .web(WebApplicationType.NONE)
                    .run(*args)
            try {
                runBlocking {
                    context.getBean(ClientScenario::class.java).run()
                }
                println("registry-messaging kotlin e2e result=passed")
            } finally {
                context.close()
            }
        }
    }

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceNodeId("kotlin-rm-client")
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addClientServerChannel(API_CHANNEL).enableClient()
            options.addClientServerChannel(WORKFLOW_CHANNEL).enableClient()
            options.addClientServerChannel("registry.messaging.api.manual")
                .enableClient(env("ZLINK_KOTLIN_E2E_API_A_ENDPOINT"))
            options.addClientServerChannel("registry.messaging.api.manual.multi")
                .enableClient(env("ZLINK_KOTLIN_E2E_API_A_ENDPOINT"))
                .enableClient(env("ZLINK_KOTLIN_E2E_API_B_ENDPOINT"))
            options.addRouteMeshChannel(ROUTE_CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_CLIENT_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from("client"))
                .enableClient(env("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(env("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setDefaultRequestTimeout(Duration.ofSeconds(2))
        }

    @Bean
    fun clientScenario(client: ZLinkClient, routes: ZLinkRouteClient): ClientScenario =
        ClientScenario(client, routes)
}

@ZLinkHandlerGroup(HANDLER_GROUP)
class ProfileRequestHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRequestHandler<ProfileRequest, ProfileReply> {
    override suspend fun handle(request: ProfileRequest, context: ZLinkRequestContext): ProfileReply {
        if (request.value == "slow") {
            delay(1000)
        }
        state.record("ProfileRequest", request.value)
        return ProfileReply("profile:${request.value}", state.providerRid, state.instanceId)
    }
}

@ZLinkHandlerGroup(HANDLER_GROUP)
class ProfileCommandHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingSendHandler<ProfileCommand> {
    override suspend fun handle(message: ProfileCommand, context: ZLinkSendContext) {
        state.record("ProfileCommand", message.commandId)
    }
}

class RoutePingHandler(
    private val state: ScenarioState,
) : ZLinkSuspendingRouteRequestHandler<RoutePing, RoutePong> {
    override suspend fun handle(request: RoutePing, context: ZLinkRouteRequestContext): RoutePong {
        state.record("ScenarioRoutePing", request.value)
        return RoutePong("route:${request.value}", state.providerRid, context.routingId().toString())
    }
}

class ClientScenario(
    private val client: ZLinkClient,
    private val routes: ZLinkRouteClient,
) {
    suspend fun run() {
        when (env("ZLINK_KOTLIN_E2E_SCENARIO", "common")) {
            "common" -> runCommon()
            "scale-out" -> runScaleOut()
            "scale-in" -> runScaleIn()
            "failover" -> runFailover()
            else -> throw IllegalArgumentException("unknown scenario ${env("ZLINK_KOTLIN_E2E_SCENARIO")}")
        }
    }

    private suspend fun runCommon() {
        runRegistryClientServer()
        runManualClientServer()
        runCrossChannelDiscovery()
        runMessageSizeVariation()
        runRouteMesh()
    }

    private suspend fun runRegistryClientServer() {
        val providers = mutableSetOf<String>()
        repeat(40) { index ->
            if (providers.size < 2) {
                val reply = request(API_CHANNEL, ProfileRequest("auto-$index"), Duration.ofSeconds(2))
                ensure(reply.value.startsWith("profile:auto-"), "RM-A1 reply payload mismatch")
                providers += reply.providerRid
            }
        }
        ensure(providers.isNotEmpty(), "RM-A1 did not reach any provider")
        println("scenario RM-A1 passed")

        val c1 = request(API_CHANNEL, ProfileRequest("c1"), Duration.ofSeconds(2))
        ensure(c1.value == "profile:c1", "RM-C1 reply mismatch")
        send(API_CHANNEL, ProfileCommand("cmd-c1"), null)
        println("scenario RM-C1 passed")

        expectFailure {
            request(API_CHANNEL, ProfileRequest("slow"), Duration.ofMillis(100))
        }
        val after = request(API_CHANNEL, ProfileRequest("after"), Duration.ofSeconds(2))
        ensure(after.value == "profile:after", "RM-C4 post-timeout request failed")
        delay(1100)
        val later = request(API_CHANNEL, ProfileRequest("later"), Duration.ofSeconds(2))
        ensure(later.value == "profile:later", "RM-C4 later request failed")
        println("scenario RM-C4 passed")

        expectFailure {
            requestWithPacket(API_CHANNEL, ProfileRequest("missing"), "MissingProfileRequest", Duration.ofSeconds(2))
        }
        send(API_CHANNEL, ProfileCommand("missing-send"), "MissingProfileCommand")
        val normal = request(API_CHANNEL, ProfileRequest("normal"), Duration.ofSeconds(2))
        ensure(normal.value == "profile:normal", "RM-C5 normal request failed")
        println("scenario RM-C5 passed")
    }

    private suspend fun runManualClientServer() {
        val manual = request("registry.messaging.api.manual", ProfileRequest("manual"), Duration.ofSeconds(2))
        ensure(manual.providerRid == "api-a", "RM-A2 wrong provider")
        println("scenario RM-A2 passed")

        val counts = mutableMapOf<String, Int>()
        repeat(80) { index ->
            val reply = request(
                "registry.messaging.api.manual.multi",
                ProfileRequest("multi-$index"),
                Duration.ofSeconds(2),
            )
            counts[reply.providerRid] = counts.getOrDefault(reply.providerRid, 0) + 1
        }
        ensure(counts.getOrDefault("api-a", 0) > 0, "RM-C3 did not reach api-a")
        ensure(counts.getOrDefault("api-b", 0) > 0, "RM-C3 did not reach api-b")
        ensure(counts.values.sum() == 80, "RM-C3 count mismatch")
        println("scenario RM-C3 passed")
    }

    private suspend fun runCrossChannelDiscovery() {
        val api = request(API_CHANNEL, ProfileRequest("a6-api"), Duration.ofSeconds(2))
        ensure(api.providerRid.startsWith("api-"), "RM-A6 api resolved wrong provider")

        val workflow = request(WORKFLOW_CHANNEL, ProfileRequest("a6-workflow"), Duration.ofSeconds(2))
        ensure(workflow.providerRid == "workflow-a", "RM-A6 workflow resolved wrong provider")
        println("scenario RM-A6 passed")
    }

    private suspend fun runMessageSizeVariation() {
        for (size in intArrayOf(8, 64 * 1024, 128 * 1024)) {
            val payload = "x".repeat(size)
            val reply = request(API_CHANNEL, ProfileRequest(payload), Duration.ofSeconds(5))
            ensure(reply.value == "profile:$payload", "RM-C8 payload mismatch for $size")
        }
        println("scenario RM-C8 roundtrip passed")
    }

    private suspend fun runRouteMesh() {
        val toB = routes.requestTo(ROUTE_CHANNEL, RoutingId.from("api-b"), RoutePing("target-b"))
            .packetName(ROUTE_PACKET)
            .timeout(Duration.ofSeconds(2))
            .awaitReply<RoutePong>()
        ensure(toB.targetRid == "api-b", "RM-C2 target rid mismatch")

        expectFailure {
            routes.requestTo(ROUTE_CHANNEL, RoutingId.from("api-missing"), RoutePing("missing"))
                .packetName(ROUTE_PACKET)
                .timeout(Duration.ofMillis(500))
                .awaitReply<RoutePong>()
        }
        println("scenario RM-C2 passed")
    }

    private suspend fun runScaleOut() {
        repeat(5) { index ->
            val reply = request(API_CHANNEL, ProfileRequest("scale-out-before-$index"), Duration.ofSeconds(2))
            ensure(reply.providerRid == "api-a", "RM-B1 initial traffic should only use api-a")
        }

        touch(env("ZLINK_KOTLIN_E2E_READY_FILE"))
        waitForFile(env("ZLINK_KOTLIN_E2E_CONTINUE_FILE"))

        val providers = mutableSetOf<String>()
        repeat(100) { index ->
            if (providers.size < 2) {
                val reply = request(API_CHANNEL, ProfileRequest("scale-out-after-$index"), Duration.ofSeconds(2))
                providers += reply.providerRid
            }
        }
        ensure(
            providers.contains("api-a") && providers.contains("api-b"),
            "RM-B1 did not route to both providers after scale-out",
        )
        println("scenario RM-B1 passed")
    }

    private suspend fun runScaleIn() {
        val before = mutableSetOf<String>()
        repeat(100) { index ->
            if (before.size < 2) {
                val reply = request(API_CHANNEL, ProfileRequest("scale-in-before-$index"), Duration.ofSeconds(2))
                before += reply.providerRid
            }
        }
        ensure(before.contains("api-a") && before.contains("api-b"), "RM-B2 did not start with both providers")

        touch(env("ZLINK_KOTLIN_E2E_READY_FILE"))
        waitForFile(env("ZLINK_KOTLIN_E2E_CONTINUE_FILE"))

        repeat(20) { index ->
            val reply = request(API_CHANNEL, ProfileRequest("scale-in-after-$index"), Duration.ofSeconds(2))
            ensure(reply.providerRid == "api-a", "RM-B2 routed to removed provider")
        }
        println("scenario RM-B2 passed")
    }

    private suspend fun runFailover() {
        val first = request(API_CHANNEL, ProfileRequest("failover-before"), Duration.ofSeconds(2))
        ensure(first.providerRid == "api-a" && first.instanceId == "api-a-v1", "RM-A4 initial provider mismatch")

        touch(env("ZLINK_KOTLIN_E2E_READY_FILE"))
        waitForFile(env("ZLINK_KOTLIN_E2E_CONTINUE_FILE"))

        repeat(20) { index ->
            val reply = request(API_CHANNEL, ProfileRequest("failover-after-$index"), Duration.ofSeconds(2))
            ensure(
                reply.providerRid == "api-a" && reply.instanceId == "api-a-v2",
                "RM-A4 did not switch to replacement provider",
            )
        }
        println("scenario RM-A4 passed")
    }

    private suspend fun request(channelName: String, request: ProfileRequest, timeout: Duration): ProfileReply =
        requestWithPacket(channelName, request, null, timeout)

    private suspend fun requestWithPacket(
        channelName: String,
        request: ProfileRequest,
        packetName: String?,
        timeout: Duration,
    ): ProfileReply {
        var call = client.requestToChannel(channelName, request).timeout(timeout)
        if (packetName != null) {
            call = call.packetName(packetName)
        }
        return call.awaitReply()
    }

    private suspend fun send(channelName: String, command: ProfileCommand, packetName: String?) {
        var call = client.sendToChannel(channelName, command)
        if (packetName != null) {
            call = call.packetName(packetName)
        }
        call.submit().await()
    }
}

private suspend fun expectFailure(action: suspend () -> Unit) {
    try {
        action()
    } catch (error: Exception) {
        return
    }
    throw IllegalStateException("operation unexpectedly succeeded")
}

private fun touch(path: String) {
    if (path.isBlank()) {
        return
    }
    Files.writeString(Path.of(path), "ready\n")
}

private suspend fun waitForFile(path: String) {
    if (path.isBlank()) {
        return
    }
    val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
    while (System.nanoTime() < deadline) {
        if (Files.exists(Path.of(path))) {
            return
        }
        delay(50)
    }
    throw IllegalStateException("timed out waiting for $path")
}

private fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}
