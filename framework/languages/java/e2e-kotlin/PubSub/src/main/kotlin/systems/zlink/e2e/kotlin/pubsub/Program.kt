package systems.zlink.e2e.kotlin.pubsub

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.charset.StandardCharsets
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkPublishHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private const val EVENT_CHANNEL = "pubsub.events"
private const val HANDLER_GROUP = "pubsub"
private const val EVENT_PACKET = "EventNotify"
private val EVIDENCE_TIMEOUT: Duration = Duration.ofSeconds(15)

fun main(args: Array<String>) {
    when (env("ZLINK_KOTLIN_E2E_ROLE", "publisher")) {
        "registry" -> RegistryApplication.run(*args)
        "subscriber" -> SubscriberApplication.run(*args)
        "publisher" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${env("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}

private fun env(name: String, fallback: String = ""): String =
    System.getenv(name)?.takeUnless { it.isBlank() } ?: fallback

data class EventNotify(val scenario: String, val sequence: Int, val value: String)
data class EvidenceEntry(
    val marker: String,
    val subscriberRid: String,
    val topic: String,
    val scenario: String,
    val sequence: Int,
    val value: String,
)
data class EvidenceSnapshot(val subscriberRid: String, val entries: List<EvidenceEntry>)

class ScenarioState(
    val subscriberRid: String,
    private val topics: Set<String>,
) {
    private val entries = mutableListOf<EvidenceEntry>()

    fun accepts(topic: String): Boolean =
        topics.contains(topic)

    @Synchronized
    fun record(marker: String, topic: String, scenario: String, sequence: Int, value: String) {
        entries += EvidenceEntry(marker, subscriberRid, topic, scenario, sequence, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(subscriberRid, entries.toList())

    companion object {
        fun topicsFromEnv(): Set<String> {
            val values = env("ZLINK_KOTLIN_E2E_TOPICS", "all")
                .split(",")
                .map { it.trim() }
                .filter { it.isNotEmpty() }
                .toMutableSet()
            values += "all"
            return values
        }
    }
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.pubsub.registry"],
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
    scanBasePackages = ["systems.zlink.e2e.kotlin.pubsub.client"],
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
                println("pub-sub kotlin e2e result=passed")
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
                .traceNodeId("kotlin-ps-client")
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addFanoutChannel(EVENT_CHANNEL)
                .enablePublisher(env("ZLINK_KOTLIN_E2E_PUBLISHER_ENDPOINT"))
        }

    @Bean
    fun clientScenario(fanout: ZLinkFanoutClient, json: ObjectMapper): ClientScenario =
        ClientScenario(fanout, json)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.pubsub.subscriber"],
)
class SubscriberApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(SubscriberApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState(env("ZLINK_KOTLIN_E2E_SUBSCRIBER_RID", "sub-1"), ScenarioState.topicsFromEnv())

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun evidenceHttpServer(state: ScenarioState, json: ObjectMapper): EvidenceHttpServer =
        EvidenceHttpServer(state, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun subscriberFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/${state.subscriberRid}-flow.log")
                .traceNodeId("kotlin-ps-${state.subscriberRid}")
                .setMessageDispatchErrorObserver { error ->
                    state.record(
                        "DispatchError",
                        error.topic(),
                        "observer",
                        -1,
                        "${error.reason()}/${error.action()}/${error.packetName()}",
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addHandlersFromPackageOf(EventNotifyHandler::class.java)
            options.useDiscovery().addRegistryEndpoint(env("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addFanoutChannel(EVENT_CHANNEL)
                .enableSubscriber()
                .addHandlerGroup(HANDLER_GROUP)
        }

    @Bean
    fun eventNotifyHandler(state: ScenarioState): EventNotifyHandler =
        EventNotifyHandler(state)
}

@ZLinkHandlerGroup(HANDLER_GROUP)
class EventNotifyHandler(
    private val state: ScenarioState,
) : ZLinkPublishHandler<EventNotify> {
    override fun handle(message: EventNotify, context: ZLinkPublishContext) {
        if (!state.accepts(context.topic())) {
            return
        }
        state.record("EventNotify", context.topic(), message.scenario, message.sequence, message.value)
    }
}

class ClientScenario(
    private val fanout: ZLinkFanoutClient,
    private val json: ObjectMapper,
) {
    private val http: HttpClient = HttpClient.newHttpClient()

    fun run() {
        touch(env("ZLINK_KOTLIN_E2E_PUBLISHER_READY_FILE"))
        waitForFile(env("ZLINK_KOTLIN_E2E_PRELATE_CONTINUE_FILE"))

        publish("all", EventNotify("prelate", 0, "before-late"))
        waitForEvent("sub-1", "prelate", 0)
        waitForEvent("sub-2", "prelate", 0)
        touch(env("ZLINK_KOTLIN_E2E_LATE_READY_FILE"))
        waitForFile(env("ZLINK_KOTLIN_E2E_LATE_CONTINUE_FILE"))

        runFanoutBasicDelivery()
        runTopicFilter()
        runLateSubscriber()
        runMissingPacket()
    }

    private fun runFanoutBasicDelivery() {
        repeat(20) { index ->
            publish("all", EventNotify("warmup", index, "warmup-$index"))
        }
        for (rid in listOf("sub-1", "sub-2", "sub-3")) {
            waitForAnyEvent(rid, "warmup")
        }

        repeat(12) { sequence ->
            publish("all", EventNotify("ps-a1", sequence, "fanout-$sequence"))
        }
        val common = commonSequences("ps-a1", setOf("sub-1", "sub-2", "sub-3"))
        ensure(hasContiguousRun(common, 4), "PS-A1 did not observe a shared contiguous sequence")
        println("scenario PS-A1 passed")
    }

    private fun runTopicFilter() {
        publish("alpha", EventNotify("ps-a2", 1, "alpha-only"))
        publish("beta", EventNotify("ps-a2", 2, "beta-only"))
        publish("gamma", EventNotify("ps-a2", 3, "gamma-only"))

        waitForEvent("sub-1", "ps-a2", 1)
        waitForEvent("sub-2", "ps-a2", 2)
        waitForEvent("sub-3", "ps-a2", 3)
        Thread.sleep(500)

        val sub1 = snapshot("sub-1")
        val sub2 = snapshot("sub-2")
        val sub3 = snapshot("sub-3")
        ensure(hasEvent(sub1, "ps-a2", 1), "PS-A2 sub-1 missed alpha")
        ensure(!hasEvent(sub1, "ps-a2", 2) && !hasEvent(sub1, "ps-a2", 3),
            "PS-A2 sub-1 recorded an uninterested topic")
        ensure(hasEvent(sub2, "ps-a2", 2), "PS-A2 sub-2 missed beta")
        ensure(!hasEvent(sub2, "ps-a2", 1) && !hasEvent(sub2, "ps-a2", 3),
            "PS-A2 sub-2 recorded an uninterested topic")
        ensure(hasEvent(sub3, "ps-a2", 3), "PS-A2 sub-3 missed gamma")
        ensure(!hasEvent(sub3, "ps-a2", 1) && !hasEvent(sub3, "ps-a2", 2),
            "PS-A2 sub-3 recorded an uninterested topic")
        println("scenario PS-A2 passed")
    }

    private fun runLateSubscriber() {
        val late = snapshot("sub-3")
        ensure(!hasEvent(late, "prelate", 0), "PS-A3 late subscriber received replayed pre-late event")
        publish("all", EventNotify("ps-a3", 1, "after-late"))
        waitForEvent("sub-3", "ps-a3", 1)
        println("scenario PS-A3 passed")
    }

    private fun runMissingPacket() {
        fanout.publish(EVENT_CHANNEL, "all", EventNotify("ps-c1", 1, "missing-packet"))
            .packetName("MissingEventNotify")
            .await()
        waitForDispatchError("sub-1", "MissingEventNotify")
        publish("all", EventNotify("ps-c1", 2, "normal-after-missing"))
        waitForEvent("sub-1", "ps-c1", 2)
        waitForEvent("sub-2", "ps-c1", 2)
        waitForEvent("sub-3", "ps-c1", 2)
        println("scenario PS-C1 passed")
    }

    private fun publish(topic: String, message: EventNotify) {
        fanout.publish(EVENT_CHANNEL, topic, message)
            .packetName(EVENT_PACKET)
            .await()
    }

    private fun commonSequences(scenario: String, subscriberRids: Set<String>): List<Int> {
        val deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos()
        while (System.nanoTime() < deadline) {
            var common: MutableList<Int>? = null
            for (rid in subscriberRids) {
                val sequences = snapshot(rid).entries
                    .filter { it.scenario == scenario }
                    .map { it.sequence }
                    .distinct()
                    .sorted()
                if (common == null) {
                    common = sequences.toMutableList()
                } else {
                    common.retainAll(sequences.toSet())
                }
            }
            if (common != null && hasContiguousRun(common, 4)) {
                return common
            }
            Thread.sleep(100)
        }
        return emptyList()
    }

    private fun waitForAnyEvent(subscriberRid: String, scenario: String) {
        waitUntil {
            snapshot(subscriberRid).entries.any { it.scenario == scenario }
        }
    }

    private fun waitForEvent(subscriberRid: String, scenario: String, sequence: Int) {
        waitUntil {
            hasEvent(snapshot(subscriberRid), scenario, sequence)
        }
    }

    private fun waitForDispatchError(subscriberRid: String, packetName: String) {
        waitUntil {
            snapshot(subscriberRid).entries.any {
                it.marker == "DispatchError"
                    && it.value.contains("HANDLER_MISSING")
                    && it.value.contains("DROP")
                    && it.value.contains(packetName)
            }
        }
    }

    private fun snapshot(subscriberRid: String): EvidenceSnapshot {
        val endpoint = when (subscriberRid) {
            "sub-1" -> env("ZLINK_KOTLIN_E2E_SUB1_HTTP")
            "sub-2" -> env("ZLINK_KOTLIN_E2E_SUB2_HTTP")
            "sub-3" -> env("ZLINK_KOTLIN_E2E_SUB3_HTTP")
            else -> throw IllegalArgumentException("unknown subscriber $subscriberRid")
        }
        try {
            val request = HttpRequest.newBuilder(URI.create("$endpoint/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            return json.readValue(response.body(), EvidenceSnapshot::class.java)
        } catch (error: Exception) {
            throw IllegalStateException("failed to fetch evidence from $subscriberRid", error)
        }
    }

    private fun waitUntil(check: () -> Boolean) {
        val deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos()
        var last: Throwable? = null
        while (System.nanoTime() < deadline) {
            try {
                if (check()) {
                    return
                }
            } catch (error: Throwable) {
                last = error
            }
            Thread.sleep(100)
        }
        throw IllegalStateException("timed out waiting for evidence", last)
    }
}

private fun hasEvent(snapshot: EvidenceSnapshot, scenario: String, sequence: Int): Boolean =
    snapshot.entries.any {
        it.marker == "EventNotify" && it.scenario == scenario && it.sequence == sequence
    }

private fun hasContiguousRun(sequences: List<Int>, minLength: Int): Boolean {
    var run = 0
    var previous = Int.MIN_VALUE
    for (value in sequences.distinct().sorted()) {
        run = if (value == previous + 1) run + 1 else 1
        if (run >= minLength) {
            return true
        }
        previous = value
    }
    return false
}

private fun touch(file: String) {
    if (file.isBlank()) {
        return
    }
    try {
        Files.createFile(Path.of(file))
    } catch (_: FileAlreadyExistsException) {
    } catch (error: Exception) {
        throw IllegalStateException("failed to create marker $file", error)
    }
}

private fun waitForFile(file: String) {
    if (file.isBlank()) {
        return
    }
    val path = Path.of(file)
    val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
    while (System.nanoTime() < deadline) {
        if (Files.exists(path)) {
            return
        }
        Thread.sleep(100)
    }
    throw IllegalStateException("timed out waiting for marker $file")
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
