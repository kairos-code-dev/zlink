package systems.zlink.e2e.kotlin.spotservice.multinode

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import java.time.Duration
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import org.springframework.core.env.Environment
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.ZLinkSpotAddress
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.multinode"]
)
class MultiNodeApplication {
    @Bean
    fun multiNodeOptions(environment: Environment): MultiNodeOptions =
        MultiNodeOptions.fromSpringEnvironment(environment)

    @Bean
    fun evidenceStore(options: MultiNodeOptions): MultiNodeEvidenceStore =
        MultiNodeEvidenceStore(options.rid, options.evidenceFile)

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun multiNodeHttpServer(
        options: MultiNodeOptions,
        json: ObjectMapper,
        spots: ZLinkSpotManager,
        routes: ZLinkRouteClient,
        evidence: MultiNodeEvidenceStore,
        context: ConfigurableApplicationContext
    ): MultiNodeHttpServer =
        MultiNodeHttpServer(options, json, spots, routes, evidence, context)

    @Bean
    fun multiNodeFramework(options: MultiNodeOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { framework ->
            Files.createDirectories(Path.of(options.logDir))
            val node = MultiNodeKind.fromRid(options.rid)
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${options.logDir}/${options.rid}-flow.log")
                .traceLabel(options.rid)
            framework.addRouteMeshChannel(node.routeChannel)
                .enableServer(requireOption(node.routeEndpoint(options), node.routeEndpointOption))
                .enableClient(requireOption(node.routeEndpoint(options), node.routeEndpointOption))
                .setRoutingId(RoutingId.from(node.rid))
            framework.addSpotMesh(node.spotMesh)
                .enableRouter(requireOption(node.spotRouterEndpoint(options), node.spotRouterEndpointOption))
                .setRoutingId(RoutingId.from(node.rid))
                .addSpotFactory(node.spotClass)
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX"))
        )

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val options = MultiNodeOptions.parse(args)
            val builder = SpringApplicationBuilder(MultiNodeApplication::class.java)
                .web(WebApplicationType.NONE)
                .properties(options.toProperties())
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }

        private fun requireOption(value: String, optionName: String): String {
            require(value.isNotBlank()) { "$optionName is required." }
            return value
        }
    }
}

class MultiNodeHttpServer(
    private val options: MultiNodeOptions,
    private val json: ObjectMapper,
    private val spots: ZLinkSpotManager,
    private val routes: ZLinkRouteClient,
    private val evidence: MultiNodeEvidenceStore,
    private val context: ConfigurableApplicationContext
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        val uri = URI.create(options.httpUrl)
        val host = if (uri.host.isNullOrBlank()) "127.0.0.1" else uri.host
        val nextServer = HttpServer.create(InetSocketAddress(host, uri.port), 0)
        nextServer.createContext("/health") { exchange ->
            writeJson(exchange, 200, mapOf("status" to "ready", "role" to "multi-node", "rid" to options.rid))
        }
        nextServer.createContext("/evidence") { exchange ->
            writeJson(exchange, 200, evidence.snapshot())
        }
        nextServer.createContext("/evidence/wait") { exchange ->
            requireMethod(exchange, "POST")
            val request = readJson(exchange, Contracts.EvidenceWaitReq::class.java)
            val snapshot = evidence.waitUntil(
                request.containsAll,
                Duration.ofMillis(request.timeoutMilliseconds.coerceIn(1, 30_000).toLong())
            )
            writeJson(exchange, 200, snapshot)
        }
        nextServer.createContext("/spot/create-local") { exchange ->
            requireMethod(exchange, "POST")
            val request = readJson(exchange, Contracts.MultiNodeCreateSpotReq::class.java)
            val created = createLocalSpot(request)
            writeJson(exchange, 200, created)
        }
        nextServer.createContext("/spot/state/request") { exchange ->
            requireMethod(exchange, "POST")
            val request = readJson(exchange, Contracts.MultiNodeStateRouteReq::class.java)
            writeJson(exchange, 200, requestState(request))
        }
        nextServer.createContext("/shutdown") { exchange ->
            writeJson(exchange, 200, mapOf("status" to "stopping"))
            Thread { context.close() }.start()
        }
        nextServer.createContext("/crash") { exchange ->
            write(exchange, 202, "accepted\n")
            Thread {
                Thread.sleep(50)
                Runtime.getRuntime().halt(137)
            }.start()
        }
        nextServer.start()
        server = nextServer
        running = true
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean =
        running

    private fun createLocalSpot(request: Contracts.MultiNodeCreateSpotReq): Contracts.MultiNodeCreateSpotRes {
        val node = MultiNodeKind.fromRid(options.rid)
        val spot = if (node == MultiNodeKind.NODE_A) {
            spots.getOrCreate(MultiNodeSpotA::class.java, RoutingId.from(request.spotRid))
        } else {
            spots.getOrCreate(MultiNodeSpotB::class.java, RoutingId.from(request.spotRid))
        }
            .toCompletableFuture()
            .join()
        val state = requestState(Contracts.MultiNodeStateRouteReq(request.spotRid, request.delta))
        evidence.add("multi-create-spot|node=${node.rid}|spot=${request.spotRid}|state=active")
        return Contracts.MultiNodeCreateSpotRes(
            request.spotRid,
            node.rid,
            "active",
            state.value
        )
    }

    private fun requestState(request: Contracts.MultiNodeStateRouteReq): Contracts.MultiNodeStateRes {
        val node = MultiNodeKind.fromRid(options.rid)
        return routes.requestToSpot(
            node.routeChannel,
            ZLinkSpotAddress(Contracts.SPOT_MESH, RoutingId.from(node.rid), RoutingId.from(request.spotRid)),
            Contracts.MultiNodeStateReq("add", request.delta)
        )
            .packetName("StateReq")
            .timeout(Duration.ofSeconds(2))
            .await(Contracts.MultiNodeStateRes::class.java)
    }

    private fun <T> readJson(exchange: HttpExchange, type: Class<T>): T =
        exchange.requestBody.use { body -> json.readValue(body, type) }

    private fun requireMethod(exchange: HttpExchange, method: String) {
        if (exchange.requestMethod != method) {
            write(exchange, 405, "method not allowed\n")
            throw IllegalStateException("HTTP ${exchange.requestMethod} is not allowed for ${exchange.requestURI.path}")
        }
    }

    private fun writeJson(exchange: HttpExchange, status: Int, value: Any) {
        val body = json.writeValueAsBytes(value)
        exchange.responseHeaders.add("Content-Type", "application/json")
        exchange.sendResponseHeaders(status, body.size.toLong())
        exchange.responseBody.write(body)
        exchange.close()
    }

    private fun write(exchange: HttpExchange, status: Int, value: String) {
        val body = value.toByteArray(StandardCharsets.UTF_8)
        exchange.sendResponseHeaders(status, body.size.toLong())
        exchange.responseBody.write(body)
        exchange.close()
    }
}

class MultiNodeEvidenceStore(
    val rid: String,
    private val evidenceFile: String
) {
    private val entries = ConcurrentLinkedQueue<String>()
    private val signal = Semaphore(0)

    fun add(value: String) {
        entries.add(value)
        signal.release()
        if (evidenceFile.isNotBlank()) {
            val path = Path.of(evidenceFile)
            Files.createDirectories(path.parent)
            Files.writeString(
                path,
                "$value\n",
                StandardCharsets.UTF_8,
                StandardOpenOption.CREATE,
                StandardOpenOption.APPEND
            )
        }
    }

    fun snapshot(): List<String> =
        entries.toList()

    fun waitUntil(expected: List<String>, timeout: Duration): List<String> {
        val deadline = System.nanoTime() + timeout.toNanos()
        while (System.nanoTime() < deadline) {
            val snapshot = snapshot()
            if (expected.all { value -> snapshot.any { it.contains(value) } }) {
                return snapshot
            }
            signal.tryAcquire(100, TimeUnit.MILLISECONDS)
        }
        throw IllegalStateException("Timed out waiting for multi-node evidence: $expected")
    }
}

data class MultiNodeOptions(
    val rid: String,
    val httpUrl: String,
    val logDir: String,
    val evidenceFile: String,
    val multiRouteAEndpoint: String,
    val multiRouteBEndpoint: String,
    val multiSpotRouterAEndpoint: String,
    val multiSpotRouterBEndpoint: String
) {
    fun toProperties(): Map<String, Any> =
        mapOf(
            "zlink.e2e.multinode.rid" to rid,
            "zlink.e2e.multinode.http-url" to httpUrl,
            "zlink.e2e.multinode.log-dir" to logDir,
            "zlink.e2e.multinode.evidence-file" to evidenceFile,
            "zlink.e2e.multinode.multi-route-a-endpoint" to multiRouteAEndpoint,
            "zlink.e2e.multinode.multi-route-b-endpoint" to multiRouteBEndpoint,
            "zlink.e2e.multinode.multi-spot-router-a-endpoint" to multiSpotRouterAEndpoint,
            "zlink.e2e.multinode.multi-spot-router-b-endpoint" to multiSpotRouterBEndpoint
        )

    companion object {
        fun parse(args: Array<out String>): MultiNodeOptions {
            val values = mutableMapOf<String, String>()
            var index = 0
            while (index < args.size) {
                val key = args[index]
                if (!key.startsWith("--")) {
                    index++
                    continue
                }
                require(index + 1 < args.size) { "Missing value for $key." }
                values[key.removePrefix("--")] = args[index + 1]
                index += 2
            }
            return MultiNodeOptions(
                rid = required(values, "rid"),
                httpUrl = required(values, "http-url"),
                logDir = values["log-dir"] ?: Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs"),
                evidenceFile = values["evidence-file"].orEmpty(),
                multiRouteAEndpoint = values["multi-route-a-endpoint"].orEmpty(),
                multiRouteBEndpoint = values["multi-route-b-endpoint"].orEmpty(),
                multiSpotRouterAEndpoint = values["multi-spot-router-a-endpoint"].orEmpty(),
                multiSpotRouterBEndpoint = values["multi-spot-router-b-endpoint"].orEmpty()
            )
        }

        fun fromSpringEnvironment(environment: Environment): MultiNodeOptions =
            MultiNodeOptions(
                rid = environment.getProperty("zlink.e2e.multinode.rid", ""),
                httpUrl = environment.getProperty("zlink.e2e.multinode.http-url", ""),
                logDir = environment.getProperty("zlink.e2e.multinode.log-dir", ""),
                evidenceFile = environment.getProperty("zlink.e2e.multinode.evidence-file", ""),
                multiRouteAEndpoint = environment.getProperty("zlink.e2e.multinode.multi-route-a-endpoint", ""),
                multiRouteBEndpoint = environment.getProperty("zlink.e2e.multinode.multi-route-b-endpoint", ""),
                multiSpotRouterAEndpoint = environment.getProperty("zlink.e2e.multinode.multi-spot-router-a-endpoint", ""),
                multiSpotRouterBEndpoint = environment.getProperty("zlink.e2e.multinode.multi-spot-router-b-endpoint", "")
            )

        private fun required(values: Map<String, String>, name: String): String {
            val value = values[name]
            require(!value.isNullOrBlank()) { "--$name is required." }
            return value
        }
    }
}

enum class MultiNodeKind(
    val rid: String,
    val routeChannel: String,
    val spotMesh: String,
    val spotClass: Class<out ZLinkSpot<*>>,
    val routeEndpointOption: String,
    val spotRouterEndpointOption: String
) {
    NODE_A(
        Contracts.MULTI_SPOT_NODE_A,
        Contracts.MULTI_ROUTE_CHANNEL_A,
        Contracts.MULTI_SPOT_NODE_A,
        MultiNodeSpotA::class.java,
        "--multi-route-a-endpoint",
        "--multi-spot-router-a-endpoint"
    ),
    NODE_B(
        Contracts.MULTI_SPOT_NODE_B,
        Contracts.MULTI_ROUTE_CHANNEL_B,
        Contracts.MULTI_SPOT_NODE_B,
        MultiNodeSpotB::class.java,
        "--multi-route-b-endpoint",
        "--multi-spot-router-b-endpoint"
    );

    fun routeEndpoint(options: MultiNodeOptions): String =
        if (this == NODE_A) options.multiRouteAEndpoint else options.multiRouteBEndpoint

    fun spotRouterEndpoint(options: MultiNodeOptions): String =
        if (this == NODE_A) options.multiSpotRouterAEndpoint else options.multiSpotRouterBEndpoint

    companion object {
        fun fromRid(rid: String): MultiNodeKind =
            entries.firstOrNull { it.rid == rid }
                ?: throw IllegalArgumentException("multi-node role requires rid '${NODE_A.rid}' or '${NODE_B.rid}'")
    }
}
