package systems.zlink.e2e.kotlin.resiliencelifecycle

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
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.springframework.beans.factory.ObjectProvider
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.service.registry.ServiceRole
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.registry.ZLinkRegistryQueryClient
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private const val CHANNEL = "resilience.lifecycle.api"
private const val HANDLER_GROUP = "resilience-lifecycle"

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

data class WorkRequest(val value: String)
data class WorkReply(val value: String, val providerRid: String)
data class EvidenceEntry(val marker: String, val providerRid: String, val value: String)
data class EvidenceSnapshot(val providerRid: String, val weight: Int, val entries: List<EvidenceEntry>)

class ScenarioState(val providerRid: String) {
    private val entries = mutableListOf<EvidenceEntry>()
    private val slowRelease = CountDownLatch(1)
    @Volatile
    private var currentWeight = 100

    @Synchronized
    fun weight(value: Int) {
        currentWeight = value
    }

    fun weight(): Int =
        currentWeight

    fun releaseSlow() {
        slowRelease.countDown()
    }

    fun awaitSlowRelease() {
        try {
            if (!slowRelease.await(20, TimeUnit.SECONDS)) {
                throw IllegalStateException("slow request was not released")
            }
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
    }

    @Synchronized
    fun record(marker: String, value: String) {
        entries += EvidenceEntry(marker, providerRid, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(providerRid, currentWeight, entries.toList())
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.resiliencelifecycle.registry"],
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
    scanBasePackages = ["systems.zlink.e2e.kotlin.resiliencelifecycle.handlers"],
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
    fun scenarioState(): ScenarioState =
        ScenarioState(env("ZLINK_KOTLIN_E2E_PROVIDER_RID", "provider-a"))

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun evidenceHttpServer(
        state: ScenarioState,
        json: ObjectMapper,
        runtimeOptions: ZLinkChannelRuntimeOptions,
    ): EvidenceHttpServer =
        EvidenceHttpServer(state, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"), runtimeOptions)

    @Bean
    fun providerFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/${state.providerRid}-flow.log")
                .traceNodeId("kotlin-rl-${state.providerRid}")
            options.addHandlersFromPackageOf(WorkRequestHandler::class.java)
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addClientServerChannel(CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .serverRoutingId(RoutingId.from(state.providerRid))
                .addHandlerGroup(HANDLER_GROUP)
        }

    @Bean
    fun workRequestHandler(state: ScenarioState): WorkRequestHandler =
        WorkRequestHandler(state)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.resiliencelifecycle.client"],
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
                println("resilience-lifecycle kotlin e2e result=passed")
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
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceNodeId("kotlin-rl-client")
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addClientServerChannel(CHANNEL).enableClient()
        }

    @Bean
    fun clientScenario(
        client: ZLinkClient,
        registry: ObjectProvider<ZLinkRegistryQueryClient>,
        json: ObjectMapper,
    ): ClientScenario =
        ClientScenario(client, registry.getIfAvailable(), json)
}

@ZLinkHandlerGroup(HANDLER_GROUP)
class WorkRequestHandler(private val state: ScenarioState) : ZLinkRequestHandler<WorkRequest, WorkReply> {
    override fun handle(request: WorkRequest, context: ZLinkRequestContext): WorkReply {
        if (request.value == "slow") {
            state.record("SlowStarted", request.value)
            state.awaitSlowRelease()
            state.record("SlowCompleted", request.value)
        } else {
            state.record("WorkRequest", request.value)
        }
        return WorkReply("work:${request.value}", state.providerRid)
    }
}

class ClientScenario(
    private val client: ZLinkClient,
    private val registry: ZLinkRegistryQueryClient?,
    private val json: ObjectMapper,
) {
    private val http: HttpClient = HttpClient.newHttpClient()

    fun run() {
        runDrainRestore()
        runDrainInFlight()
    }

    private fun runDrainRestore() {
        waitForTopology(2)
        val warm = collectProviders("b4-warm", 80, 2)
        ensure(warm.contains("api-a") && warm.contains("api-b"), "RL-B4 warmup did not reach both providers: $warm")

        post("${adminA()}/admin/drain")
        waitForWeight(adminA(), 0)
        collectStableProvidersWithout("b4-drained", "api-a", "api-b")
        ensure(get("${adminA()}/health").contains("ok"), "RL-B4 drained provider health failed")
        waitForTopology(2)

        post("${adminA()}/admin/restore")
        waitForWeight(adminA(), 100)
        val restored = collectProviders("b4-restored", 120, 2)
        ensure(restored.contains("api-a"), "RL-B4 restored provider did not receive traffic")
        println("scenario RL-B4 passed")
    }

    private fun runDrainInFlight() {
        post("${adminB()}/admin/drain")
        waitForWeight(adminB(), 0)
        Thread.sleep(1500)

        val slow: CompletionStage<WorkReply> = client.requestToChannel(CHANNEL, WorkRequest("slow"))
            .timeout(Duration.ofSeconds(15))
            .submit(WorkReply::class.java)
        waitForEvidence(adminA(), "SlowStarted")

        post("${adminA()}/admin/drain")
        waitForWeight(adminA(), 0)
        post("${adminB()}/admin/restore")
        waitForWeight(adminB(), 100)
        collectStableProvidersWithout("b5-after-drain", "api-a", "api-b")

        post("${adminA()}/admin/release-slow")
        val slowReply = try {
            slow.toCompletableFuture().get(20, TimeUnit.SECONDS)
        } catch (error: Exception) {
            throw IllegalStateException("RL-B5 slow request did not complete", error)
        }
        ensure(slowReply.providerRid == "api-a", "RL-B5 slow request was not served by api-a: ${slowReply.providerRid}")
        ensure(slowReply.value == "work:slow", "RL-B5 slow reply payload mismatch")

        post("${adminA()}/admin/restore")
        waitForWeight(adminA(), 100)
        println("scenario RL-B5 passed")
    }

    private fun collectProviders(prefix: String, attempts: Int, expectedCount: Int): Set<String> {
        val providers = mutableSetOf<String>()
        for (index in 0 until attempts) {
            if (providers.size >= expectedCount) {
                break
            }
            val reply = client.requestToChannel(CHANNEL, WorkRequest("$prefix-$index"))
                .timeout(Duration.ofSeconds(3))
                .await(WorkReply::class.java)
            ensure(reply.value == "work:$prefix-$index", "reply payload mismatch for $prefix-$index")
            providers += reply.providerRid
        }
        return providers
    }

    private fun collectStableProvidersWithout(prefix: String, forbidden: String, required: String): Set<String> {
        repeat(30) { window ->
            val providers = collectProviders("$prefix-window-$window", 5, 1)
            if (!providers.contains(forbidden) && providers.contains(required)) {
                return providers
            }
            Thread.sleep(300)
        }
        throw IllegalStateException("$prefix did not converge away from $forbidden to $required")
    }

    private fun waitForTopology(expectedRouters: Int) {
        val query = registry ?: return
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (System.nanoTime() < deadline) {
            try {
                val count = query.topology().toCompletableFuture()
                    .get(3, TimeUnit.SECONDS)
                    .filter { it.channelName() == CHANNEL }
                    .filter { it.serviceRole() == ServiceRole.ROUTER }
                    .count()
                if (count >= expectedRouters) {
                    return
                }
            } catch (_: Exception) {
            }
            Thread.sleep(200)
        }
        throw IllegalStateException("registry topology did not report $expectedRouters routers for $CHANNEL")
    }

    private fun waitForWeight(baseUrl: String, expected: Int) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (System.nanoTime() < deadline) {
            try {
                val node: JsonNode = json.readTree(get("$baseUrl/admin/weight"))
                if (node.path("weight").asInt(-1) == expected) {
                    return
                }
            } catch (_: Exception) {
            }
            Thread.sleep(100)
        }
        throw IllegalStateException("weight did not become $expected for $baseUrl")
    }

    private fun waitForEvidence(baseUrl: String, marker: String) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (System.nanoTime() < deadline) {
            try {
                val entries = json.readTree(get("$baseUrl/evidence")).path("entries")
                if (entries.isArray) {
                    for (entry in entries) {
                        if (marker == entry.path("marker").asText()) {
                            return
                        }
                    }
                }
            } catch (_: Exception) {
            }
            Thread.sleep(100)
        }
        throw IllegalStateException("marker $marker was not observed at $baseUrl")
    }

    private fun adminA(): String =
        env("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT")

    private fun adminB(): String =
        env("ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT")

    private fun get(url: String): String {
        try {
            val request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(5))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            ensure(response.statusCode() in 200..299, "GET $url returned ${response.statusCode()}")
            return response.body()
        } catch (error: IOException) {
            throw IllegalStateException("GET failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("GET interrupted: $url", error)
        }
    }

    private fun post(url: String) {
        try {
            val request = HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(5))
                .POST(HttpRequest.BodyPublishers.noBody())
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            ensure(response.statusCode() in 200..299, "POST $url returned ${response.statusCode()}")
        } catch (error: IOException) {
            throw IllegalStateException("POST failed: $url", error)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("POST interrupted: $url", error)
        }
    }
}

private fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}

class EvidenceHttpServer(
    private val state: ScenarioState,
    private val json: ObjectMapper,
    private val endpoint: String,
    private val runtimeOptions: ZLinkChannelRuntimeOptions,
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
            nextServer.createContext("/health") { exchange -> write(exchange, 200, "ok\n") }
            nextServer.createContext("/evidence") { exchange ->
                val body = json.writeValueAsBytes(state.snapshot())
                exchange.responseHeaders.add("Content-Type", "application/json")
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.use { it.write(body) }
            }
            nextServer.createContext("/admin/drain") { exchange -> setWeight(exchange, 0, "drained") }
            nextServer.createContext("/admin/restore") { exchange -> setWeight(exchange, 100, "restored") }
            nextServer.createContext("/admin/weight") { exchange ->
                write(exchange, 200, "{\"weight\":${state.weight()}}\n")
            }
            nextServer.createContext("/admin/release-slow") { exchange ->
                state.releaseSlow()
                write(exchange, 200, "{\"status\":\"released\"}\n")
            }
            nextServer.start()
            server = nextServer
            running = true
        } catch (error: Exception) {
            throw IllegalStateException("failed to start evidence endpoint $endpoint", error)
        }
    }

    private fun setWeight(exchange: HttpExchange, weight: Int, status: String) {
        runtimeOptions.clientServerChannel(CHANNEL)
            .configureServerSocket()
            .weight(weight)
        state.weight(weight)
        state.record("AdminWeight", weight.toString())
        write(exchange, 200, "{\"status\":\"$status\",\"weight\":$weight}\n")
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean =
        running

    companion object {
        private fun write(exchange: HttpExchange, status: Int, value: String) {
            val body = value.toByteArray(StandardCharsets.UTF_8)
            exchange.sendResponseHeaders(status, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
    }
}
