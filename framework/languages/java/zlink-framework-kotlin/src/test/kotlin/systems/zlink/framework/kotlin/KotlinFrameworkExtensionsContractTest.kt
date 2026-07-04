package systems.zlink.framework.kotlin

import java.time.Duration
import java.util.Optional
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertSame
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkSubmitStage
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorDirectory
import systems.zlink.framework.actors.ZLinkActorJoinCall
import systems.zlink.framework.actors.ZLinkActorJoinResult
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall
import systems.zlink.framework.actors.ZLinkActorPlacement
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind
import systems.zlink.framework.errors.ZLinkFrameworkException
import systems.zlink.framework.locations.ZLinkSpotAddress
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResult
import systems.zlink.framework.spots.ZLinkSpotCreateState
import systems.zlink.framework.spots.ZLinkSpotInfo
import systems.zlink.framework.spots.ZLinkSpotManager

class KotlinFrameworkExtensionsContractTest {
    @Test
    fun `directory object ensure extension delegates to Java ensure overload`() = runBlocking {
        val directory = RecordingActorDirectory(ACTOR_REF)

        val result = directory.ensureActor("actor-a", CreateActor("payload"))

        assertEquals(ACTOR_REF, result)
        assertEquals("actor-a", directory.actorId)
        assertEquals(CreateActor("payload"), directory.request?.decode(CreateActor::class.java))
    }

    @Test
    fun `actor ref snapshot extension delegates to shared snapshot model`() {
        val snapshot = ACTOR_REF.snapshot()

        assertEquals(ACTOR_REF.nodeRid(), snapshot.nodeRid())
        assertEquals(ACTOR_REF.actorId(), snapshot.actorId())
        assertEquals(ACTOR_REF.generation(), snapshot.generation())
        assertEquals(ACTOR_REF, snapshot.actorRef())
    }

    @Test
    fun `join await extension awaits submit without calling blocking Java await`() = runBlocking {
        val call = RecordingJoinCall(ZLinkActorJoinResult(1, ACTOR_REF, "joined"))

        val result = call.awaitJoin<String>()

        assertEquals("joined", result.reply())
        assertEquals(String::class.java, call.replyType)
    }

    @Test
    fun `yield join extension keeps existing Java yield path`() = runBlocking {
        val call = RecordingYieldJoinCall(ZLinkActorJoinResult(1, ACTOR_REF, "yielded"))

        val result = yield<String>(call)

        assertEquals("yielded", result.reply())
        assertEquals(String::class.java, call.replyType)
    }

    @Test
    fun `route spot address extensions delegate to Java spot address surface`() = runBlocking {
        val route = RecordingRouteClient("reply")
        val message = Message.from("request")

        route.send("route-channel", SPOT_ADDRESS, message)
        val reply = route.request<String>("route-channel", SPOT_ADDRESS, message)

        assertEquals("reply", reply)
        assertEquals("route-channel", route.sendChannel)
        assertEquals(SPOT_ADDRESS, route.sendAddress)
        assertSame(message, route.sendMessage)
        assertEquals("route-channel", route.requestChannel)
        assertEquals(SPOT_ADDRESS, route.requestAddress)
        assertSame(message, route.requestMessage)
    }

    @Test
    fun `typed spot manager extensions delegate to Java class based surface`() = runBlocking {
        val manager = RecordingSpotManager()

        val created = manager.create<TestSpot>()
        val createdWithRequest = manager.create<TestSpot>(ZLinkMessage.of(CreateActor("request")))
        val createdWithRid = manager.create<TestSpot>(SPOT_RID)
        val existing = manager.getOrCreate<TestSpot>(SPOT_RID)
        val existingWithRequest = manager.getOrCreate<TestSpot>(
            SPOT_RID,
            ZLinkMessage.of(CreateActor("get-or-create")),
        )

        assertTrue(
            listOf(created, createdWithRequest, createdWithRid, existing, existingWithRequest)
                .all { it.spotRid() == SPOT_RID },
        )
        assertEquals(
            listOf(
                "create:TestSpot",
                "create-request:TestSpot",
                "create-rid:TestSpot",
                "get-or-create:TestSpot",
                "get-or-create-request:TestSpot",
            ),
            manager.calls,
        )
    }

    @Test
    fun `framework error kind is preserved across coroutine await boundary`() = runBlocking {
        val call = FailingRequestCall(
            ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
                "stale actor location",
            ),
        )

        val error = assertThrows<ZLinkFrameworkException> {
            runBlocking {
                call.awaitReply<String>()
            }
        }

        assertEquals(ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE, error.kind())
        assertTrue(error.retriable())
    }

    private data class CreateActor(val value: String)

    private class RecordingActorDirectory(
        private val actorRef: ZLinkActorRef,
    ) : ZLinkActorDirectory {
        var actorId: String? = null
        var request: ZLinkMessage? = null

        override fun find(actorId: String): CompletionStage<Optional<ZLinkActorRef>> =
            CompletableFuture.completedFuture(Optional.empty())

        override fun ensure(
            actorId: String,
            createRequest: ZLinkMessage,
            placement: ZLinkActorPlacement,
        ): CompletionStage<ZLinkActorRef> {
            this.actorId = actorId
            this.request = createRequest
            return CompletableFuture.completedFuture(actorRef)
        }
    }

    private class RecordingJoinCall<TReply>(
        private val result: ZLinkActorJoinResult<TReply>,
    ) : ZLinkActorJoinCall {
        var replyType: Class<*>? = null

        override fun submit(): CompletionStage<ZLinkActorJoinResult<Void>> =
            CompletableFuture.completedFuture(ZLinkActorJoinResult(1, ACTOR_REF, null))

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<ZLinkActorJoinResult<T>> {
            this.replyType = replyType
            @Suppress("UNCHECKED_CAST")
            return CompletableFuture.completedFuture(result as ZLinkActorJoinResult<T>)
        }
    }

    private class RecordingYieldJoinCall<TReply>(
        private val result: ZLinkActorJoinResult<TReply>,
    ) : ZLinkActorJoinSpotCall {
        var replyType: Class<*>? = null

        override fun timeout(timeout: Duration): ZLinkActorJoinSpotCall = this

        override fun submit(): CompletionStage<ZLinkActorJoinResult<Void>> =
            CompletableFuture.completedFuture(ZLinkActorJoinResult(1, ACTOR_REF, null))

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<ZLinkActorJoinResult<T>> {
            this.replyType = replyType
            @Suppress("UNCHECKED_CAST")
            return CompletableFuture.completedFuture(result as ZLinkActorJoinResult<T>)
        }

        override fun yield(): ZLinkActorJoinResult<Void> =
            ZLinkActorJoinResult(1, ACTOR_REF, null)

        override fun yield(cancellationToken: CancellationToken): ZLinkActorJoinResult<Void> =
            yield()

        override fun <T : Any?> yield(replyType: Class<T>): ZLinkActorJoinResult<T> {
            this.replyType = replyType
            @Suppress("UNCHECKED_CAST")
            return result as ZLinkActorJoinResult<T>
        }

        override fun <T : Any?> yield(
            replyType: Class<T>,
            cancellationToken: CancellationToken,
        ): ZLinkActorJoinResult<T> =
            yield(replyType)
    }

    private class RecordingRouteClient<TReply>(
        private val reply: TReply,
    ) : ZLinkRouteClient {
        var sendChannel: String? = null
        var sendAddress: ZLinkSpotAddress? = null
        var sendMessage: Any? = null
        var requestChannel: String? = null
        var requestAddress: ZLinkSpotAddress? = null
        var requestMessage: Any? = null

        override fun sendToNode(channelName: String, target: RoutingId, message: Any): ZLinkSendCall =
            RecordingSendCall()

        override fun sendToSpot(channelName: String, address: ZLinkSpotAddress, message: Any): ZLinkSendCall {
            sendChannel = channelName
            sendAddress = address
            sendMessage = message
            return RecordingSendCall()
        }

        override fun requestToNode(channelName: String, target: RoutingId, message: Any): ZLinkRequestCall =
            RecordingRequestCall(reply)

        override fun requestToSpot(channelName: String, address: ZLinkSpotAddress, message: Any): ZLinkRequestCall {
            requestChannel = channelName
            requestAddress = address
            requestMessage = message
            return RecordingRequestCall(reply)
        }
    }

    private class RecordingSendCall : ZLinkSendCall {
        override fun packetName(packetName: String): ZLinkSendCall = this

        override fun metadata(key: String, value: String): ZLinkSendCall = this

        override fun submit(): ZLinkSubmitStage = ZLinkSubmitStage.completed()
    }

    private class RecordingRequestCall<TReply>(
        private val reply: TReply,
    ) : ZLinkRequestCall {
        override fun packetName(packetName: String): ZLinkRequestCall = this

        override fun metadata(key: String, value: String): ZLinkRequestCall = this

        override fun timeout(timeout: Duration): ZLinkRequestCall = this

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<T> =
            CompletableFuture.completedFuture(replyType.cast(reply))
    }

    private class FailingRequestCall(
        private val error: Throwable,
    ) : ZLinkRequestCall {
        override fun packetName(packetName: String): ZLinkRequestCall = this

        override fun metadata(key: String, value: String): ZLinkRequestCall = this

        override fun timeout(timeout: Duration): ZLinkRequestCall = this

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<T> {
            val future = CompletableFuture<T>()
            future.completeExceptionally(error)
            return future
        }
    }

    private class RecordingSpotManager : ZLinkSpotManager {
        val calls = mutableListOf<String>()

        override fun create(spotType: Class<out ZLinkSpot<*>>): CompletionStage<ZLinkSpotCreateResult> {
            calls += "create:${spotType.simpleName}"
            return result()
        }

        override fun create(
            spotType: Class<out ZLinkSpot<*>>,
            request: ZLinkMessage,
        ): CompletionStage<ZLinkSpotCreateResult> {
            calls += "create-request:${spotType.simpleName}"
            return result()
        }

        override fun create(
            spotType: Class<out ZLinkSpot<*>>,
            spotRid: RoutingId,
        ): CompletionStage<ZLinkSpotCreateResult> {
            calls += "create-rid:${spotType.simpleName}"
            return result()
        }

        override fun getOrCreate(
            spotType: Class<out ZLinkSpot<*>>,
            spotRid: RoutingId,
        ): CompletionStage<ZLinkSpotCreateResult> {
            calls += "get-or-create:${spotType.simpleName}"
            return result()
        }

        override fun getOrCreate(
            spotType: Class<out ZLinkSpot<*>>,
            spotRid: RoutingId,
            request: ZLinkMessage,
        ): CompletionStage<ZLinkSpotCreateResult> {
            calls += "get-or-create-request:${spotType.simpleName}"
            return result()
        }

        override fun find(spotRid: RoutingId): CompletionStage<Optional<ZLinkSpotInfo>> =
            CompletableFuture.completedFuture(Optional.empty())

        override fun list(): CompletionStage<List<ZLinkSpotInfo>> =
            CompletableFuture.completedFuture(listOf())

        override fun close(spotRid: RoutingId): CompletionStage<Boolean> =
            CompletableFuture.completedFuture(true)

        private fun result(): CompletionStage<ZLinkSpotCreateResult> =
            CompletableFuture.completedFuture(
                ZLinkSpotCreateResult(SPOT_RID, ZLinkSpotCreateState.CREATED, ZLinkMessage.empty()),
            )
    }

    private class TestActor : ZLinkActor {
        override fun actorId(): String = "actor-a"

        override fun context(): ZLinkActorContext =
            throw UnsupportedOperationException("test actor has no runtime context")
    }

    private class TestSpot : ZLinkSpot<TestActor> {
        override fun context(): ZLinkSpotContext =
            throw UnsupportedOperationException("test spot has no runtime context")
    }

    companion object {
        private val NODE_RID: RoutingId = RoutingId.from(byteArrayOf(0x01))
        private val SPOT_RID: RoutingId = RoutingId.from(byteArrayOf(0x02))
        private val ACTOR_REF = ZLinkActorRef(NODE_RID, "actor-a", 7)
        private val SPOT_ADDRESS = ZLinkSpotAddress("mesh", NODE_RID, SPOT_RID)
    }
}
