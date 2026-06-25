package systems.zlink.e2e.kotlin.registrymessaging

import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.util.concurrent.TimeUnit
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRouteClient

class ClientScenario(
    private val client: ZLinkClient,
    private val routes: ZLinkRouteClient,
) {
    fun run() {
        when (Env.get("ZLINK_KOTLIN_E2E_SCENARIO", "common")) {
            "common" -> runCommon()
            "scale-out" -> runScaleOut()
            "scale-in" -> runScaleIn()
            "failover" -> runFailover()
            "weighted" -> runWeightedDistribution()
            else -> throw IllegalArgumentException(
                "unknown scenario ${Env.get("ZLINK_KOTLIN_E2E_SCENARIO")}",
            )
        }
    }

    private fun runCommon() {
        runRegistryClientServer()
        runManualClientServer()
        runCrossChannelDiscovery()
        runMessageSizeVariation()
        runBackpressureObservation()
        runRouteMesh()
    }

    private fun runRegistryClientServer() {
        val providers = mutableSetOf<String>()
        var index = 0
        while (index < 40 && providers.size < 2) {
            val reply = request(
                Contracts.API_CHANNEL,
                ProfileRequest("auto-$index"),
                Duration.ofSeconds(2),
            )
            ensure(reply.value.startsWith("profile:auto-"), "RM-A1 reply payload mismatch")
            providers += reply.providerRid
            index++
        }
        ensure(providers.isNotEmpty(), "RM-A1 did not reach any provider")
        println("scenario RM-A1 passed")

        val c1 = request(Contracts.API_CHANNEL, ProfileRequest("c1"), Duration.ofSeconds(2))
        ensure(c1.value == "profile:c1", "RM-C1 reply mismatch")
        send(Contracts.API_CHANNEL, ProfileCommand("cmd-c1"), null)
        println("scenario RM-C1 passed")

        expectFailure {
            request(Contracts.API_CHANNEL, ProfileRequest("slow"), Duration.ofMillis(100))
        }
        val after = request(Contracts.API_CHANNEL, ProfileRequest("after"), Duration.ofSeconds(2))
        ensure(after.value == "profile:after", "RM-C4 post-timeout request failed")
        sleep(1100)
        val later = request(Contracts.API_CHANNEL, ProfileRequest("later"), Duration.ofSeconds(2))
        ensure(later.value == "profile:later", "RM-C4 later request failed")
        println("scenario RM-C4 passed")

        expectFailure {
            requestWithPacket(
                Contracts.API_CHANNEL,
                ProfileRequest("missing"),
                "MissingProfileRequest",
                Duration.ofSeconds(2),
            )
        }
        send(Contracts.API_CHANNEL, ProfileCommand("missing-send"), "MissingProfileCommand")
        val normal = request(Contracts.API_CHANNEL, ProfileRequest("normal"), Duration.ofSeconds(2))
        ensure(normal.value == "profile:normal", "RM-C5 normal request failed")
        println("scenario RM-C5 passed")
    }

    private fun runManualClientServer() {
        val manual = request(
            "registry.messaging.kotlin.api.manual",
            ProfileRequest("manual"),
            Duration.ofSeconds(2),
        )
        ensure(manual.providerRid == "api-a", "RM-A2 wrong provider")
        println("scenario RM-A2 passed")

        val counts = mutableMapOf<String, Int>()
        for (index in 0 until 80) {
            val reply = request(
                "registry.messaging.kotlin.api.manual.multi",
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

    private fun runWeightedDistribution() {
        val counts = mutableMapOf<String, Int>()
        for (index in 0 until 300) {
            val reply = request(
                "registry.messaging.kotlin.api.manual.multi",
                ProfileRequest("weighted-$index"),
                Duration.ofSeconds(2),
            )
            counts[reply.providerRid] = counts.getOrDefault(reply.providerRid, 0) + 1
        }
        val high = counts.getOrDefault("api-a", 0)
        val low = counts.getOrDefault("api-b", 0)
        ensure(high > 0 && low > 0, "RM-C7 did not reach both providers: $counts")
        ensure(high > low, "RM-C7 high weight provider was not preferred: $counts")
        ensure(high + low == 300, "RM-C7 count mismatch: $counts")
        println("scenario RM-C7 passed")
    }

    private fun runCrossChannelDiscovery() {
        val api = request(Contracts.API_CHANNEL, ProfileRequest("a6-api"), Duration.ofSeconds(2))
        ensure(api.providerRid.startsWith("api-"), "RM-A6 api resolved wrong provider")

        val workflow = request(
            Contracts.WORKFLOW_CHANNEL,
            ProfileRequest("a6-workflow"),
            Duration.ofSeconds(2),
        )
        ensure(workflow.providerRid == "workflow-a", "RM-A6 workflow resolved wrong provider")
        println("scenario RM-A6 passed")
    }

    private fun runMessageSizeVariation() {
        val sizes = intArrayOf(8, 64 * 1024, 128 * 1024)
        for (size in sizes) {
            val payload = "x".repeat(size)
            val reply = request(Contracts.API_CHANNEL, ProfileRequest(payload), Duration.ofSeconds(5))
            ensure(reply.value == "profile:$payload", "RM-C8 payload mismatch for $size")
        }
        println("scenario RM-C8 passed")
    }

    private fun runBackpressureObservation() {
        val requests = (0 until 40).map { index ->
            client.requestToChannel(Contracts.API_CHANNEL, ProfileRequest("slow-c9-$index"))
                .timeout(Duration.ofMillis(150))
                .submit(ProfileReply::class.java)
        }

        var failed = 0
        for (request in requests) {
            try {
                request.toCompletableFuture().get(2, TimeUnit.SECONDS)
            } catch (expected: Exception) {
                failed++
            }
        }
        ensure(failed > 0, "RM-C9 did not observe bounded backpressure/timeout")
        sleep(5000)

        val recovered = request(
            Contracts.WORKFLOW_CHANNEL,
            ProfileRequest("c9-recovered"),
            Duration.ofSeconds(5),
        )
        ensure(recovered.value == "profile:c9-recovered", "RM-C9 connection did not recover after pressure")
        println("scenario RM-C9 passed")
    }

    private fun runRouteMesh() {
        val toB = routes.requestTo(
            Contracts.ROUTE_CHANNEL,
            RoutingId.from("api-b"),
            RoutePing("target-b"),
        )
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(Duration.ofSeconds(2))
            .await(RoutePong::class.java)
        ensure(toB.targetRid == "api-b", "RM-C2 target rid mismatch")

        expectFailure {
            routes.requestTo(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("api-missing"),
                RoutePing("missing"),
            )
                .packetName(Contracts.ROUTE_PACKET)
                .timeout(Duration.ofMillis(500))
                .await(RoutePong::class.java)
        }
        println("scenario RM-C2 passed")
    }

    private fun runScaleOut() {
        for (index in 0 until 5) {
            val reply = request(
                Contracts.API_CHANNEL,
                ProfileRequest("scale-out-before-$index"),
                Duration.ofSeconds(2),
            )
            ensure(reply.providerRid == "api-a", "RM-B1 initial traffic should only use api-a")
        }

        touch(Env.get("ZLINK_KOTLIN_E2E_READY_FILE"))
        waitForFile(Env.get("ZLINK_KOTLIN_E2E_CONTINUE_FILE"))

        val providers = mutableSetOf<String>()
        var index = 0
        while (index < 100 && providers.size < 2) {
            val reply = request(
                Contracts.API_CHANNEL,
                ProfileRequest("scale-out-after-$index"),
                Duration.ofSeconds(2),
            )
            providers += reply.providerRid
            index++
        }
        ensure(
            providers.contains("api-a") && providers.contains("api-b"),
            "RM-B1 did not route to both providers after scale-out",
        )
        println("scenario RM-B1 passed")
    }

    private fun runScaleIn() {
        val before = mutableSetOf<String>()
        var index = 0
        while (index < 100 && before.size < 2) {
            val reply = request(
                Contracts.API_CHANNEL,
                ProfileRequest("scale-in-before-$index"),
                Duration.ofSeconds(2),
            )
            before += reply.providerRid
            index++
        }
        ensure(before.contains("api-a") && before.contains("api-b"), "RM-B2 did not start with both providers")

        touch(Env.get("ZLINK_KOTLIN_E2E_READY_FILE"))
        waitForFile(Env.get("ZLINK_KOTLIN_E2E_CONTINUE_FILE"))

        for (afterIndex in 0 until 20) {
            val reply = request(
                Contracts.API_CHANNEL,
                ProfileRequest("scale-in-after-$afterIndex"),
                Duration.ofSeconds(2),
            )
            ensure(reply.providerRid == "api-a", "RM-B2 routed to removed provider")
        }
        println("scenario RM-B2 passed")
    }

    private fun runFailover() {
        val first = request(
            Contracts.API_CHANNEL,
            ProfileRequest("failover-before"),
            Duration.ofSeconds(2),
        )
        ensure(
            first.providerRid == "api-a" && first.instanceId == "api-a-v1",
            "RM-A4 initial provider mismatch",
        )

        touch(Env.get("ZLINK_KOTLIN_E2E_READY_FILE"))
        waitForFile(Env.get("ZLINK_KOTLIN_E2E_CONTINUE_FILE"))

        for (index in 0 until 20) {
            val reply = request(
                Contracts.API_CHANNEL,
                ProfileRequest("failover-after-$index"),
                Duration.ofSeconds(2),
            )
            ensure(
                reply.providerRid == "api-a" && reply.instanceId == "api-a-v2",
                "RM-A4 did not switch to replacement provider",
            )
        }
        println("scenario RM-A4 passed")
    }

    private fun request(
        channelName: String,
        request: ProfileRequest,
        timeout: Duration,
    ): ProfileReply =
        requestWithPacket(channelName, request, null, timeout)

    private fun requestWithPacket(
        channelName: String,
        request: ProfileRequest,
        packetName: String?,
        timeout: Duration,
    ): ProfileReply {
        var call = client.requestToChannel(channelName, request).timeout(timeout)
        if (packetName != null) {
            call = call.packetName(packetName)
        }
        return call.await(ProfileReply::class.java)
    }

    private fun send(
        channelName: String,
        command: ProfileCommand,
        packetName: String?,
    ) {
        var call = client.sendToChannel(channelName, command)
        if (packetName != null) {
            call = call.packetName(packetName)
        }
        call.await()
    }

    private fun expectFailure(action: () -> Unit) {
        try {
            action()
        } catch (error: RuntimeException) {
            return
        }
        throw IllegalStateException("operation unexpectedly succeeded")
    }

    private fun touch(path: String) {
        if (path.isBlank()) {
            return
        }
        try {
            Files.writeString(Path.of(path), "ready\n")
        } catch (error: java.io.IOException) {
            throw IllegalStateException("failed to write $path", error)
        }
    }

    private fun waitForFile(path: String) {
        if (path.isBlank()) {
            return
        }
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < deadline) {
            if (Files.exists(Path.of(path))) {
                return
            }
            sleep(50)
        }
        throw IllegalStateException("timed out waiting for $path")
    }

    private fun sleep(millis: Long) {
        try {
            Thread.sleep(millis)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
    }

    private fun ensure(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException(message)
        }
    }
}
