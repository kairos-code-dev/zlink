package systems.zlink.framework.kotlin.scenarioe2e

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.time.Instant
import java.util.concurrent.CompletionException
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.TimeoutException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.springframework.boot.SpringBootConfiguration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.EnableAutoConfiguration
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.handlers.ZLinkSend
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.httpclient.kotlin.awaitRaw
import systems.zlink.httpclient.kotlin.fetch
import systems.zlink.httpclient.kotlin.zlinkHttpClient

object ScenarioE2E {
    private val mapper = jacksonObjectMapper().findAndRegisterModules()
    private lateinit var currentOptions: ScenarioOptions

    @JvmStatic
    fun main(args: Array<String>) = runBlocking {
        require(args.isNotEmpty()) { "scenario E2E role is required." }
        val role = args[0]
        val options = ScenarioOptions.parse(args.drop(1))
        currentOptions = options
        when (role) {
            "server" -> runServer(options)
            "client" -> runClient(options)
            else -> throw IllegalArgumentException("Unknown scenario E2E role '$role'.")
        }
    }

    private suspend fun runServer(options: ScenarioOptions) {
        Files.createDirectories(Path.of(options.workDir))
        val builder = SpringApplicationBuilder(ScenarioServerApplication::class.java)
            .web(WebApplicationType.NONE)
        builder.application().setKeepAlive(true)
        builder.run().use { context ->
            val evidence = context.getBean(ScenarioEvidence::class.java)
            val server = HttpServer.create(InetSocketAddress("127.0.0.1", URI.create(options.httpEndpoint).port), 0)
            server.createContext("/health") { exchange -> writeJson(exchange, 200, mapOf("status" to "ready")) }
            server.createContext("/evidence") { exchange ->
                writeJson(
                    exchange,
                    200,
                    ScenarioEvidenceSnapshot(
                        evidence.serverId,
                        evidence.snapshotRequestIds(),
                        evidence.snapshotCommandIds(),
                        evidence.snapshotCompletedRequestIds(),
                        evidence.snapshotRouteRequestIds(),
                        evidence.snapshotRouteSourceRids(),
                    ),
                )
            }
            server.start()
            Files.writeString(Path.of(options.readyFile), "ready")
            waitForShutdown()
            server.stop(0)
        }
    }

    private suspend fun runClient(options: ScenarioOptions) {
        Files.createDirectories(Path.of(options.workDir))
        val scenario = mapper.readValue<ScenarioDocument>(Path.of(options.scenarioFile).toFile())

        ensure(scenario.id == "CH-001" || scenario.id == "CH-002" || scenario.id == "CH-004" ||
            scenario.id == "CH-006" || scenario.id == "CH-007",
            "scenario id must be CH-001, CH-002, CH-004, CH-006, or CH-007")
        ensure(scenario.roles.server.any { it.channel == options.channel },
            "scenario server channel must match the configured channel")
        ensure(scenario.roles.client.isNotEmpty(), "scenario must declare at least one client role")
        ensure(Path.of(options.workDir, scenario.artifacts.logs).toAbsolutePath().normalize() ==
            Path.of(options.logDir).toAbsolutePath().normalize(),
            "scenario logs artifact must match configured log directory")
        ensure(Path.of(options.workDir, scenario.artifacts.report).toAbsolutePath().normalize() ==
            Path.of(options.reportFile).toAbsolutePath().normalize(),
            "scenario report artifact must match configured report file")
        ensure(scenario.artifacts.evidence == "server:/evidence" || scenario.artifacts.evidence == "servers:/evidence",
            "scenario evidence artifact must use supported evidence endpoint")

        val builder = SpringApplicationBuilder(ScenarioClientApplication::class.java)
            .web(WebApplicationType.NONE)
        builder.run().use { context ->
            val client = context.getBean(ZLinkClient::class.java)
            val httpClients = options.httpEndpoints.map { endpoint ->
                zlinkHttpClient(endpoint) {
                    json()
                    timeout(Duration.ofSeconds(3))
                }
            }
            val http = httpClients.first()
            val repliesByServer = mutableMapOf<String, Int>()
            try {
                for (step in scenario.steps) {
                    if (step.waitReady != null) {
                        val waitReady = step.waitReady
                        val targets = waitReady.targets ?: listOf(waitReady.target)
                        val selectedHttpClients = if (targets.size == 1) listOf(http) else httpClients
                        ensure(selectedHttpClients.size == targets.size,
                            "readiness endpoint count must match server targets")
                        var serverReady = false
                        val deadline = Instant.now().plusSeconds(10)
                        while (Instant.now().isBefore(deadline) && !serverReady) {
                            serverReady = true
                            for (endpoint in selectedHttpClients) {
                                serverReady = try {
                                    serverReady && endpoint.get("/health").awaitRaw().status() == 200
                                } catch (_: RuntimeException) {
                                    false
                                }
                            }
                            if (!serverReady) {
                                delay(100)
                            }
                        }

                        ensure(targets.all { it.startsWith("server:") }, "readiness targets must be server roles")
                        ensure(serverReady, "server readiness must be reached before client request")
                        continue
                    }

                    if (step.clientRequest != null) {
                        val request = step.clientRequest
                        ensure(request.client == scenario.roles.client.first().name,
                            "request client must match scenario role")
                        ensure(request.channel == options.channel, "request channel must match configured channel")
                        ensure(request.packetName == "ScenarioProfileRequest",
                            "request packet name must match handler")

                        val reply = client
                            .requestToChannel(
                                request.channel,
                                ScenarioProfileRequest(request.requestId, request.value),
                            )
                            .packetName(request.packetName)
                            .timeout(Duration.ofSeconds(3))
                            .awaitReply<ScenarioProfileReply>()

                        ensure(reply.requestId == request.requestId, "reply request id must match")
                        ensure(reply.value == request.expectReply, "reply value must match scenario expectation")
                        if (scenario.expect.reply.requestId == request.requestId) {
                            ensure(reply.requestId == scenario.expect.reply.requestId,
                                "reply request id must match expect block")
                            ensure(reply.value == scenario.expect.reply.value,
                                "reply value must match expect block")
                        }
                        continue
                    }

                    if (step.clientTimeoutRequest != null) {
                        val request = step.clientTimeoutRequest
                        ensure(request.client == scenario.roles.client.first().name,
                            "timeout request client must match scenario role")
                        ensure(request.channel == options.channel,
                            "timeout request channel must match configured channel")
                        ensure(request.packetName == "ScenarioProfileRequest",
                            "timeout request packet name must match handler")

                        try {
                            client
                                .requestToChannel(
                                    request.channel,
                                    ScenarioProfileRequest(request.requestId, request.value),
                                )
                                .packetName(request.packetName)
                                .timeout(Duration.ofMillis(request.timeoutMs.toLong()))
                                .awaitReply<ScenarioProfileReply>()
                            ensure(false, "timeout request must fail with public timeout error")
                        } catch (error: Throwable) {
                            ensure(generateSequence(error as Throwable?) { it.cause }.any { it is TimeoutException },
                                "timeout request must fail with java.util.concurrent.TimeoutException")
                        }
                        continue
                    }

                    if (step.concurrentTimeoutAndRequest != null) {
                        val request = step.concurrentTimeoutAndRequest
                        ensure(request.client == scenario.roles.client.first().name,
                            "concurrent request client must match scenario role")
                        ensure(request.channel == options.channel,
                            "concurrent request channel must match configured channel")
                        ensure(request.packetName == "ScenarioProfileRequest",
                            "concurrent request packet name must match handler")

                        val slowRequest = client
                            .requestToChannel(
                                request.channel,
                                ScenarioProfileRequest(request.timeoutRequestId, request.timeoutValue),
                            )
                            .packetName(request.packetName)
                            .timeout(Duration.ofMillis(request.timeoutMs.toLong()))
                            .submit(ScenarioProfileReply::class.java)
                            .toCompletableFuture()

                        val reply = client
                            .requestToChannel(
                                request.channel,
                                ScenarioProfileRequest(request.normalRequestId, request.normalValue),
                            )
                            .packetName(request.packetName)
                            .timeout(Duration.ofSeconds(3))
                            .awaitReply<ScenarioProfileReply>()
                        ensure(reply.requestId == request.normalRequestId,
                            "concurrent normal reply request id must match")
                        ensure(reply.value == request.expectNormalReply,
                            "concurrent normal reply value must match")

                        try {
                            slowRequest.join()
                            ensure(false, "concurrent slow request must fail with public timeout error")
                        } catch (error: CompletionException) {
                            ensure(generateSequence(error as Throwable?) { it.cause }.any { it is TimeoutException },
                                "concurrent slow request must fail with java.util.concurrent.TimeoutException")
                        }
                        continue
                    }

                    if (step.waitFor != null) {
                        val waitFor = step.waitFor
                        ensure(waitFor.durationMs > 0, "waitFor duration must be positive")
                        delay(waitFor.durationMs.toLong())
                        continue
                    }

                    if (step.roundRobinRequest != null) {
                        val request = step.roundRobinRequest
                        ensure(request.client == scenario.roles.client.first().name,
                            "request client must match scenario role")
                        ensure(request.channel == options.channel, "request channel must match configured channel")
                        ensure(request.packetName == "ScenarioProfileRequest",
                            "request packet name must match handler")
                        ensure(request.requestCount == 90, "CH-002 request count must be 90")
                        ensure(request.warmupCount == 3, "CH-002 warm-up count must be 3")

                        for (index in 0 until request.warmupCount) {
                            val requestId = "${request.requestIdPrefix}-warmup-$index"
                            val value = "${request.valuePrefix}-warmup-$index"
                            val reply = client
                                .requestToChannel(
                                    request.channel,
                                    ScenarioProfileRequest(requestId, value),
                                )
                                .packetName(request.packetName)
                                .timeout(Duration.ofSeconds(3))
                                .awaitReply<ScenarioProfileReply>()
                            ensure(reply.requestId == requestId, "warm-up reply request id must match")
                        }

                        for (index in 0 until request.requestCount) {
                            val requestId = "${request.requestIdPrefix}-$index"
                            val value = "${request.valuePrefix}-$index"
                            val reply = client
                                .requestToChannel(
                                    request.channel,
                                    ScenarioProfileRequest(requestId, value),
                                )
                                .packetName(request.packetName)
                                .timeout(Duration.ofSeconds(3))
                                .awaitReply<ScenarioProfileReply>()
                            ensure(reply.requestId == requestId, "round-robin reply request id must match")
                            ensure(reply.value == "profile:$value", "round-robin reply value must match")
                            ensure(reply.serverId.isNotBlank(), "round-robin reply must include server id")
                            repliesByServer[reply.serverId] = (repliesByServer[reply.serverId] ?: 0) + 1
                        }
                        continue
                    }

                    if (step.routeTargetedRequest != null) {
                        val routeClient = context.getBean(ZLinkRouteClient::class.java)
                        val request = step.routeTargetedRequest
                        ensure(request.client == scenario.roles.client.first().name,
                            "route request client must match scenario role")
                        ensure(request.channel == options.channel, "route request channel must match configured channel")
                        ensure(request.packetName == "ScenarioRoutePing",
                            "route request packet name must match handler")

                        val reply = routeClient
                            .requestTo(
                                request.channel,
                                RoutingId.from(request.targetRid),
                                ScenarioRoutePingRequest(request.requestId, request.value),
                            )
                            .packetName(request.packetName)
                            .timeout(Duration.ofSeconds(3))
                            .awaitReply<ScenarioRoutePingReply>()
                        ensure(reply.requestId == request.requestId, "route reply request id must match")
                        ensure(reply.value == request.expectReply, "route reply value must match")
                        ensure(reply.targetRid == request.targetRid, "route reply target routing id must match")
                        ensure(reply.sourceRid == request.sourceRid, "route reply source routing id must match")

                        try {
                            routeClient
                                .requestTo(
                                    request.channel,
                                    RoutingId.from(request.missingRid),
                                    ScenarioRoutePingRequest("${request.requestId}-missing", request.value),
                                )
                                .packetName(request.packetName)
                                .timeout(Duration.ofMillis(request.missingTimeoutMs.toLong()))
                                .awaitReply<ScenarioRoutePingReply>()
                            ensure(false, "missing route request must fail with public route error")
                        } catch (error: RuntimeException) {
                            ensure(isPublicRouteError(error),
                                "missing route request must expose public route error: $error")
                        }

                        val httpByServer = scenario.roles.server
                            .mapIndexed { index, server -> server.name to httpClients[index] }
                            .toMap()
                        val targetHttp = httpByServer[request.targetRid]
                        val otherHttp = httpByServer[request.nonTargetRid]
                        ensure(targetHttp != null, "target route peer evidence endpoint must exist")
                        ensure(otherHttp != null, "non-target route peer evidence endpoint must exist")

                        var targetEvidence = ScenarioEvidenceSnapshot()
                        var targetMatched = false
                        val deadline = Instant.now().plusSeconds(10)
                        while (Instant.now().isBefore(deadline) && !targetMatched) {
                            targetEvidence = targetHttp!!.get("/evidence").fetch()
                            targetMatched = targetEvidence.routeRequestIds.contains(request.requestId)
                                && targetEvidence.routeSourceRids.contains(request.sourceRid)
                            if (!targetMatched) {
                                delay(100)
                            }
                        }
                        ensure(targetMatched, "target route peer evidence must include request and source routing id")
                        ensure(targetEvidence.routeRequestIds.containsAll(scenario.expect.serverEvidence.routeRequestIds),
                            "target route peer evidence must include expected route request ids")
                        ensure(targetEvidence.routeSourceRids.containsAll(scenario.expect.serverEvidence.routeSourceRids),
                            "target route peer evidence must include expected source routing ids")

                        val nonTargetDeadline = Instant.now().plusMillis(1000)
                        while (Instant.now().isBefore(nonTargetDeadline)) {
                            val otherEvidence = otherHttp!!.get("/evidence").fetch<ScenarioEvidenceSnapshot>()
                            ensure(!otherEvidence.routeRequestIds.contains(request.requestId),
                                "non-target route peer evidence must not include targeted request")
                            ensure(scenario.expect.serverEvidence.routeRequestIds.none { expected ->
                                otherEvidence.routeRequestIds.contains(expected)
                            }, "non-target route peer evidence must not include expected route request ids")
                            delay(100)
                        }
                        continue
                    }

                    if (step.assertEvidence != null) {
                        val evidenceStep = step.assertEvidence
                        ensure(evidenceStep.target == "server:api-a", "evidence target must be server:api-a")
                        var evidence = ScenarioEvidenceSnapshot()
                        val deadline = Instant.now().plusSeconds(10)
                        var evidenceMatched = false
                        while (Instant.now().isBefore(deadline) && !evidenceMatched) {
                            evidence = http.get("/evidence").fetch<ScenarioEvidenceSnapshot>()
                            val requestMatched = evidenceStep.requestId.isBlank()
                                || evidence.requestIds.contains(evidenceStep.requestId)
                            val commandMatched = evidenceStep.commandId.isBlank()
                                || evidence.commandIds.contains(evidenceStep.commandId)
                            val completedMatched = evidenceStep.completedRequestId.isBlank()
                                || evidence.completedRequestIds.contains(evidenceStep.completedRequestId)
                            evidenceMatched = requestMatched && commandMatched && completedMatched
                            if (!evidenceMatched) {
                                delay(100)
                            }
                        }
                        ensure(evidenceMatched, "server evidence must match scenario step before timeout")
                        if (evidenceStep.requestId.isNotBlank()) {
                            ensure(evidence.requestIds.contains(evidenceStep.requestId),
                                "server evidence must include request id from step")
                        }
                        if (evidenceStep.commandId.isNotBlank()) {
                            ensure(evidence.commandIds.contains(evidenceStep.commandId),
                                "server evidence must include command id from step")
                        }
                        if (evidenceStep.completedRequestId.isNotBlank()) {
                            ensure(evidence.completedRequestIds.contains(evidenceStep.completedRequestId),
                                "server evidence must include completed request id from step")
                        }
                        ensure(evidence.requestIds.containsAll(scenario.expect.serverEvidence.requestIds),
                            "server evidence must include all expected request ids")
                        ensure(evidence.commandIds.containsAll(scenario.expect.serverEvidence.commandIds),
                            "server evidence must include all expected command ids")
                        ensure(evidence.completedRequestIds.containsAll(scenario.expect.serverEvidence.completedRequestIds),
                            "server evidence must include all expected completed request ids")
                        continue
                    }

                    if (step.clientSend != null) {
                        val send = step.clientSend
                        ensure(send.client == scenario.roles.client.first().name,
                            "send client must match scenario role")
                        ensure(send.channel == options.channel, "send channel must match configured channel")
                        ensure(send.packetName == "ScenarioProfileCommand",
                            "send packet name must match handler")
                        client
                            .sendToChannel(send.channel, ScenarioProfileCommand(send.commandId, send.value))
                            .packetName(send.packetName)
                            .submit()
                            .toCompletableFuture()
                            .join()
                        continue
                    }

                    if (step.assertDistribution != null) {
                        for ((serverId, expectedCount) in step.assertDistribution.expectedCounts) {
                            ensure(repliesByServer[serverId] == expectedCount,
                                "round-robin reply count for $serverId must be $expectedCount")
                        }

                        for (endpoint in httpClients) {
                            val evidence = endpoint.get("/evidence").fetch<ScenarioEvidenceSnapshot>()
                            val expectedCount = step.assertDistribution.expectedCounts[evidence.serverId]
                            ensure(expectedCount != null, "server evidence must identify a known server")
                            val verifiedRequests = evidence.requestIds.count { requestId ->
                                requestId.startsWith("${scenario.expect.reply.requestIdPrefix}-")
                                    && !requestId.contains("-warmup-")
                            }
                            ensure(verifiedRequests == expectedCount,
                                "server evidence count for ${evidence.serverId} must be $expectedCount")
                        }
                        continue
                    }

                    throw IllegalStateException("scenario step must contain a known action.")
                }

                val report = ScenarioReport(
                    scenarioId = scenario.id,
                    language = "kotlin",
                    runtimeVersion = System.getProperty("java.version"),
                    transportBackend = "tcp",
                    codec = "json",
                    result = "passed",
                    scenarioFile = options.scenarioFile,
                    processCount = scenario.roles.server.size + scenario.roles.client.size,
                    passed = 1,
                    failed = 0,
                    skipped = 0,
                    logPath = options.logDir,
                    evidencePath = options.httpEndpoints.joinToString(",") { "$it/evidence" },
                    executedScriptPath = options.scriptPath,
                    processIds = options.processIds(scenario.roles.client),
                    failureLayer = null,
                    completedAt = Instant.now().toString(),
                )
                mapper.writerWithDefaultPrettyPrinter().writeValue(Path.of(options.reportFile).toFile(), report)
                println("scenario=${scenario.id} language=kotlin result=passed evidence=${options.reportFile}")
            } finally {
                httpClients.forEach { it.close() }
            }
        }
    }

    private fun writeJson(exchange: HttpExchange, status: Int, body: Any) {
        exchange.use {
            val bytes = mapper.writeValueAsBytes(body)
            it.responseHeaders.set("content-type", "application/json")
            it.sendResponseHeaders(status, bytes.size.toLong())
            it.responseBody.write(bytes)
        }
    }

    private suspend fun waitForShutdown() {
        val monitor = Object()
        Runtime.getRuntime().addShutdownHook(Thread {
            synchronized(monitor) {
                monitor.notifyAll()
            }
        })
        synchronized(monitor) {
            monitor.wait()
        }
    }

    private fun ensure(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException("ensure failed: $message")
        }
    }

    private fun isPublicRouteError(error: Throwable): Boolean =
        generateSequence(error as Throwable?) { it.cause }.any { current ->
            current is TimeoutException || current.message?.lowercase()?.let { message ->
                message.contains("timeout") ||
                    message.contains("not connected") ||
                    message.contains("not_connected") ||
                    message.contains("not ready") ||
                    message.contains("host unreachable") ||
                    message.contains("routenotconnected")
            } == true
        }

    @EnableZLinkFramework
    @EnableAutoConfiguration
    @SpringBootConfiguration(proxyBeanMethods = false)
    class ScenarioServerApplication {
        @Bean
        fun scenarioEvidence(): ScenarioEvidence = ScenarioEvidence()

        @Bean
        fun scenarioProfileHandler(evidence: ScenarioEvidence): ScenarioProfileHandler =
            ScenarioProfileHandler(evidence)

        @Bean
        fun scenarioRoutePingHandler(evidence: ScenarioEvidence): ScenarioRoutePingHandler =
            ScenarioRoutePingHandler(evidence)

        @Bean
        fun scenarioServerFramework(): ZLinkFrameworkConfigurer =
            ZLinkFrameworkConfigurer { options ->
                options.useCoroutineHandlers(Dispatchers.Default)
                options.codecs().addJson()
                if (currentOptions.mode == "route-peer") {
                    val routed = options.addRouteMeshChannel(currentOptions.channel)
                        .enableServer(currentOptions.zlinkEndpoint)
                    routed.configureRouting().setRoutingId(RoutingId.from(currentOptions.serverName))
                    for (endpoint in currentOptions.routePeerEndpoints) {
                        routed.enableClient(endpoint)
                    }
                    routed.addRequestHandler(
                        ScenarioRoutePingHandler::class.java,
                        ScenarioRoutePingRequest::class.java,
                        ScenarioRoutePingReply::class.java,
                        "ScenarioRoutePing",
                    )
                    return@ZLinkFrameworkConfigurer
                }

                options.addHandlersFromPackageOf(ScenarioE2E::class.java)
                options.addClientServerChannel(currentOptions.channel)
                    .enableServer(currentOptions.zlinkEndpoint)
                    .addHandlerGroup("scenario")
            }
    }

    @EnableZLinkFramework
    @EnableAutoConfiguration
    @SpringBootConfiguration(proxyBeanMethods = false)
    class ScenarioClientApplication {
        @Bean
        fun scenarioClientFramework(): ZLinkFrameworkConfigurer =
            ZLinkFrameworkConfigurer { options ->
                options.codecs().addJson()
                if (currentOptions.mode == "route-peer") {
                    val routed = options.addRouteMeshChannel(currentOptions.channel)
                        .enableServer(currentOptions.zlinkEndpoint)
                    routed.configureRouting().setRoutingId(RoutingId.from(currentOptions.clientRoutingId))
                    for (endpoint in currentOptions.routePeerEndpoints) {
                        routed.enableClient(endpoint)
                    }
                    return@ZLinkFrameworkConfigurer
                }

                val channel = options.addClientServerChannel(currentOptions.channel)
                for (endpoint in currentOptions.zlinkEndpoints) {
                    channel.enableClient(endpoint)
                }
            }
    }

    fun currentServerName(): String = currentOptions.serverName
}

@ZLinkHandlerGroup("scenario")
class ScenarioProfileHandler(
    private val evidence: ScenarioEvidence,
) {
    @ZLinkRequest(packetName = "ScenarioProfileRequest")
    suspend fun handle(request: ScenarioProfileRequest, context: ZLinkRequestContext): ScenarioProfileReply {
        evidence.requestIds.add(request.requestId)
        if (request.value == "slow" || request.requestId.contains("timeout")) {
            delay(1000)
        }
        evidence.completedRequestIds.add(request.requestId)
        return ScenarioProfileReply(request.requestId, "profile:${request.value}", evidence.serverId)
    }

    @ZLinkSend(packetName = "ScenarioProfileCommand")
    suspend fun handleCommand(command: ScenarioProfileCommand, context: ZLinkSendContext) {
        evidence.commandIds.add(command.commandId)
    }
}

data class ScenarioProfileRequest(
    val requestId: String = "",
    val value: String = "",
)

data class ScenarioProfileReply(
    val requestId: String = "",
    val value: String = "",
    val serverId: String = "",
)

data class ScenarioProfileCommand(
    val commandId: String = "",
    val value: String = "",
)

data class ScenarioRoutePingRequest(
    val requestId: String = "",
    val value: String = "",
)

data class ScenarioRoutePingReply(
    val requestId: String = "",
    val value: String = "",
    val targetRid: String = "",
    val sourceRid: String = "",
)

class ScenarioRoutePingHandler(
    private val evidence: ScenarioEvidence,
) : ZLinkRouteRequestHandler<ScenarioRoutePingRequest, ScenarioRoutePingReply> {
    override fun handle(
        request: ScenarioRoutePingRequest,
        context: ZLinkRouteRequestContext,
    ): ScenarioRoutePingReply {
        evidence.routeRequestIds.add(request.requestId)
        evidence.routeSourceRids.add(context.routingId().toString())
        return ScenarioRoutePingReply(
            request.requestId,
            "route:${request.value}",
            evidence.serverId,
            context.routingId().toString(),
        )
    }
}

class ScenarioEvidence {
    val serverId: String = ScenarioE2E.currentServerName()
    val requestIds: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
    val commandIds: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
    val completedRequestIds: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
    val routeRequestIds: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
    val routeSourceRids: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()

    fun snapshotRequestIds(): List<String> = requestIds.toList()

    fun snapshotCommandIds(): List<String> = commandIds.toList()

    fun snapshotCompletedRequestIds(): List<String> = completedRequestIds.toList()

    fun snapshotRouteRequestIds(): List<String> = routeRequestIds.toList()

    fun snapshotRouteSourceRids(): List<String> = routeSourceRids.toList()
}

data class ScenarioEvidenceSnapshot(
    val serverId: String = "",
    val requestIds: List<String> = emptyList(),
    val commandIds: List<String> = emptyList(),
    val completedRequestIds: List<String> = emptyList(),
    val routeRequestIds: List<String> = emptyList(),
    val routeSourceRids: List<String> = emptyList(),
)

data class ScenarioDocument(
    val id: String = "",
    val name: String = "",
    val roles: ScenarioRoles = ScenarioRoles(),
    val steps: List<ScenarioStep> = emptyList(),
    val expect: ScenarioExpect = ScenarioExpect(),
    val artifacts: ScenarioArtifacts = ScenarioArtifacts(),
)

data class ScenarioRoles(
    val registry: ScenarioRegistryRole = ScenarioRegistryRole(),
    val server: List<ScenarioServerRole> = emptyList(),
    val client: List<ScenarioClientRole> = emptyList(),
)

data class ScenarioRegistryRole(val count: Int = 0)

data class ScenarioServerRole(
    val name: String = "",
    val channel: String = "",
)

data class ScenarioClientRole(
    val name: String = "",
    val routingId: String = "",
)

data class ScenarioStep(
    val waitReady: ScenarioWaitReadyStep? = null,
    val clientRequest: ScenarioClientRequestStep? = null,
    val clientTimeoutRequest: ScenarioClientTimeoutRequestStep? = null,
    val concurrentTimeoutAndRequest: ScenarioConcurrentTimeoutAndRequestStep? = null,
    val waitFor: ScenarioWaitForStep? = null,
    val clientSend: ScenarioClientSendStep? = null,
    val roundRobinRequest: ScenarioRoundRobinRequestStep? = null,
    val routeTargetedRequest: ScenarioRouteTargetedRequestStep? = null,
    val assertEvidence: ScenarioAssertEvidenceStep? = null,
    val assertDistribution: ScenarioAssertDistributionStep? = null,
)

data class ScenarioWaitReadyStep(
    val target: String = "",
    val targets: List<String>? = null,
)

data class ScenarioClientRequestStep(
    val client: String = "",
    val channel: String = "",
    val packetName: String = "",
    val requestId: String = "",
    val value: String = "",
    val expectReply: String = "",
)

data class ScenarioClientTimeoutRequestStep(
    val client: String = "",
    val channel: String = "",
    val packetName: String = "",
    val requestId: String = "",
    val value: String = "",
    val timeoutMs: Int = 0,
)

data class ScenarioConcurrentTimeoutAndRequestStep(
    val client: String = "",
    val channel: String = "",
    val packetName: String = "",
    val timeoutRequestId: String = "",
    val timeoutValue: String = "",
    val timeoutMs: Int = 0,
    val normalRequestId: String = "",
    val normalValue: String = "",
    val expectNormalReply: String = "",
)

data class ScenarioWaitForStep(
    val durationMs: Int = 0,
)

data class ScenarioClientSendStep(
    val client: String = "",
    val channel: String = "",
    val packetName: String = "",
    val commandId: String = "",
    val value: String = "",
)

data class ScenarioAssertEvidenceStep(
    val target: String = "",
    val requestId: String = "",
    val commandId: String = "",
    val completedRequestId: String = "",
)

data class ScenarioRoundRobinRequestStep(
    val client: String = "",
    val channel: String = "",
    val packetName: String = "",
    val warmupCount: Int = 0,
    val requestCount: Int = 0,
    val requestIdPrefix: String = "",
    val valuePrefix: String = "",
)

data class ScenarioRouteTargetedRequestStep(
    val client: String = "",
    val channel: String = "",
    val packetName: String = "",
    val targetRid: String = "",
    val nonTargetRid: String = "",
    val sourceRid: String = "",
    val missingRid: String = "",
    val missingTimeoutMs: Int = 0,
    val requestId: String = "",
    val value: String = "",
    val expectReply: String = "",
)

data class ScenarioAssertDistributionStep(
    val expectedCounts: Map<String, Int> = emptyMap(),
)

data class ScenarioExpect(
    val reply: ScenarioReplyExpect = ScenarioReplyExpect(),
    val serverEvidence: ScenarioServerEvidenceExpect = ScenarioServerEvidenceExpect(),
)

data class ScenarioReplyExpect(
    val requestId: String = "",
    val value: String = "",
    val requestIdPrefix: String = "",
    val valuePrefix: String = "",
)

data class ScenarioServerEvidenceExpect(
    val requestIds: List<String> = emptyList(),
    val commandIds: List<String> = emptyList(),
    val completedRequestIds: List<String> = emptyList(),
    val routeRequestIds: List<String> = emptyList(),
    val routeSourceRids: List<String> = emptyList(),
    val requestCounts: Map<String, Int> = emptyMap(),
)

data class ScenarioArtifacts(
    val logs: String = "",
    val report: String = "",
    val evidence: String = "",
)

data class ScenarioReport(
    val scenarioId: String,
    val language: String,
    val runtimeVersion: String,
    val transportBackend: String,
    val codec: String,
    val result: String,
    val scenarioFile: String,
    val processCount: Int,
    val passed: Int,
    val failed: Int,
    val skipped: Int,
    val logPath: String,
    val evidencePath: String,
    val executedScriptPath: String,
    val processIds: Map<String, Long>,
    val failureLayer: String?,
    val completedAt: String,
)

data class ScenarioOptions(
    val workDir: String,
    val scenarioFile: String,
    val zlinkEndpoint: String,
    val httpEndpoint: String,
    val channel: String,
    val readyFile: String,
    val reportFile: String,
    val logDir: String,
    val scriptPath: String,
    val serverProcessIds: String,
    val serverName: String,
    val mode: String,
    val routePeerEndpoint: String,
    val clientRoutingId: String,
) {
    val zlinkEndpoints: List<String>
        get() = zlinkEndpoint.split(",").map { it.trim() }.filter { it.isNotEmpty() }

    val httpEndpoints: List<String>
        get() = httpEndpoint.split(",").map { it.trim() }.filter { it.isNotEmpty() }

    val routePeerEndpoints: List<String>
        get() = routePeerEndpoint.split(",").map { it.trim() }.filter { it.isNotEmpty() }

    fun processIds(clientRoles: List<ScenarioClientRole>): Map<String, Long> {
        val result = mutableMapOf("client:${clientRoles.first().name}" to ProcessHandle.current().pid())
        serverProcessIds.split(",")
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .forEach { entry ->
                val parts = entry.split("=", limit = 2)
                require(parts.size == 2 && parts[0].isNotBlank() && parts[1].isNotBlank()) {
                    "server process id entry must use role=pid."
                }
                result[parts[0]] = parts[1].toLong()
            }
        return result.toMap()
    }

    companion object {
        fun parse(args: List<String>): ScenarioOptions {
            val values = mutableMapOf<String, String>()
            var index = 0
            while (index < args.size) {
                require(index + 1 < args.size && args[index].startsWith("--")) {
                    "scenario E2E arguments must use --name value pairs."
                }
                values[args[index].removePrefix("--")] = args[index + 1]
                index += 2
            }
            val workDir = required(values, "work-dir")
            return ScenarioOptions(
                workDir = workDir,
                scenarioFile = values["scenario"] ?: "",
                zlinkEndpoint = required(values, "zlink-endpoint"),
                httpEndpoint = required(values, "http-endpoint"),
                channel = values["channel"] ?: "scenario-api",
                readyFile = values["ready-file"] ?: Path.of(workDir, "server.ready").toString(),
                reportFile = values["report-file"] ?: Path.of(workDir, "report.json").toString(),
                logDir = values["log-dir"] ?: Path.of(workDir, "logs").toString(),
                scriptPath = values["script-path"] ?: "",
                serverProcessIds = values["server-process-ids"] ?: "",
                serverName = values["server-name"] ?: "api-a",
                mode = values["mode"] ?: "channel",
                routePeerEndpoint = values["route-peer-endpoints"] ?: "",
                clientRoutingId = values["client-routing-id"] ?: "client-a",
            )
        }

        private fun required(values: Map<String, String>, name: String): String {
            val value = values[name]
            require(!value.isNullOrBlank()) { "--$name is required." }
            return value
        }
    }
}
