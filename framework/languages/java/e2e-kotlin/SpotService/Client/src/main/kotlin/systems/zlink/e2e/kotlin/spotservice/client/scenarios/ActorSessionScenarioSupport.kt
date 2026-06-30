package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.awaitUnchecked
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure
import systems.zlink.e2e.kotlin.spotservice.client.support.postJson
import systems.zlink.stream.connector.ZLinkStreamConnector

internal class ActorSessionScenarioContext : AutoCloseable {
    private val connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
    private val unbound = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
    private val inboundNames = CopyOnWriteArrayList<String>()
    private val inboundObserver = connector.observeInbound { observation ->
        inboundNames.add(observation.packetName())
        CompletableFuture.completedFuture(null)
    }

    val profile = Contracts.ActorProfile(
        "Player One",
        7,
        listOf("alpha", "beta"),
    )
    lateinit var auth: Contracts.ActorAuthReply
        private set
    lateinit var entryReply: Contracts.ActorEchoReply
        private set
    lateinit var entryPush: Contracts.ActorPush
        private set
    lateinit var joined: Contracts.ActorJoinReply
        private set
    lateinit var userReply1: Contracts.ActorEchoReply
        private set
    lateinit var userPush1: Contracts.ActorPush
        private set
    lateinit var userReply2: Contracts.ActorEchoReply
        private set
    lateinit var userPush2: Contracts.ActorPush
        private set
    lateinit var userReply3: Contracts.ActorEchoReply
        private set
    lateinit var userPush3: Contracts.ActorPush
        private set

    fun observedInboundNames(): List<String> = inboundNames.toList()

    fun connectAndAuthenticate() {
        connector.connect().await()
        unbound.connect().await()
        auth = connector
            .request(Contracts.ActorAuthRequest("actor-local-1", profile))
            .await(Contracts.ActorAuthReply::class.java)
    }

    fun requestEntryEcho() {
        val push = connector.waitFor(Contracts.ActorPush::class.java)
            .submit(Contracts.ActorPush::class.java)
        entryReply = connector
            .request(Contracts.ActorEchoRequest("entry-echo", 1, profile))
            .metadata("actor-id", "actor-local-1")
            .await(Contracts.ActorEchoReply::class.java)
        entryPush = connector.await(push).payload()
    }

    fun joinRoom() {
        joined = connector
            .request(Contracts.ActorJoinRequest("room-a", profile, profile.tags))
            .metadata("actor-id", "actor-local-1")
            .await(Contracts.ActorJoinReply::class.java)
    }

    fun requestFirstUserEchoAndCheckUnboundIsolation() {
        val unboundPush = unbound.waitFor(Contracts.ActorPush::class.java)
            .timeout(Duration.ofMillis(400))
            .submit(Contracts.ActorPush::class.java)
        val push = connector.waitFor(Contracts.ActorPush::class.java)
            .submit(Contracts.ActorPush::class.java)
        userReply1 = requestUserEcho("user-echo-1", 2)
        userPush1 = connector.await(push).payload()
        expectFailure { awaitUnchecked(unbound, unboundPush) }
    }

    fun requestOrderedUserEchoes() {
        val secondPush = connector.waitFor(Contracts.ActorPush::class.java)
            .submit(Contracts.ActorPush::class.java)
        userReply2 = requestUserEcho("user-echo-2", 3)
        userPush2 = connector.await(secondPush).payload()

        val thirdPush = connector.waitFor(Contracts.ActorPush::class.java)
            .submit(Contracts.ActorPush::class.java)
        userReply3 = requestUserEcho("user-echo-3", 4)
        userPush3 = connector.await(thirdPush).payload()
    }

    private fun requestUserEcho(value: String, seq: Int): Contracts.ActorEchoReply =
        connector
            .request(Contracts.ActorEchoRequest(value, seq, profile))
            .metadata("actor-id", "actor-local-1")
            .await(Contracts.ActorEchoReply::class.java)

    override fun close() {
        closeQuietly(inboundObserver)
        closeQuietly(connector)
        closeQuietly(unbound)
    }

    private fun closeQuietly(registration: AutoCloseable) {
        try {
            registration.close()
        } catch (_: Exception) {
        }
    }

    private fun closeQuietly(stream: ZLinkStreamConnector) {
        try {
            stream.close().await()
        } catch (_: Exception) {
        }
    }
}

internal object ActorSessionScenarioSupport {
    fun run(block: (ActorSessionScenarioContext) -> Unit) {
        ActorSessionScenarioContext().use { context ->
            try {
                block(context)
                waitForSessionEvidence()
            } catch (error: Exception) {
                throw IllegalStateException("actor/session scenario failed", error)
            }
        }
    }

    private fun waitForSessionEvidence() {
        postJson(
            Env.get("ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT"),
            "/evidence/wait",
            Contracts.EvidenceWaitRequest(
                listOf(
                    "ActorCreated",
                    "ActorCreatedPayload",
                    "Player One/7/alpha,beta",
                    "ActorUserJoined",
                    "ActorUserRequest",
                    "user-echo-3",
                    "StreamInbound",
                ),
                10_000,
            ),
            Contracts.EvidenceSnapshot::class.java,
        )
    }
}
