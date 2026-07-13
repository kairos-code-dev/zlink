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
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorDirectory
import systems.zlink.framework.actors.ZLinkActorJoinCall
import systems.zlink.framework.actors.ZLinkActorJoinResult
import systems.zlink.framework.actors.ZLinkActorPlacement
import systems.zlink.framework.actors.ZLinkActorRequestCall
import systems.zlink.framework.actors.ZLinkActorSendCall
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind
import systems.zlink.framework.errors.ZLinkFrameworkException
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
        val call = RecordingJoinCall(ZLinkActorJoinResult.Accepted(ACTOR_REF, "joined"))

        val result = call.awaitJoinReply<String>()

        assertEquals("joined", result.reply())
        assertEquals(String::class.java, call.replyType)
    }

    @Test
    fun `actor request extension delegates to Java actor ref client call`() = runBlocking {
        val actorClient = RecordingActorClient(ActorReply("reply"))

        val reply = actorClient.requestToActorAwait<ActorReply>(ACTOR_REF, ActorMessage("request"))

        assertEquals(ActorReply("reply"), reply)
        assertEquals(ACTOR_REF, actorClient.requestedActorRef)
        assertEquals(ActorMessage("request"), actorClient.requestedMessage)
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

    private data class ActorMessage(val value: String)

    private data class ActorReply(val value: String)

    private class RecordingActorDirectory(
        private val actorRef: ActorRef,
    ) : ZLinkActorDirectory {
        var actorId: String? = null
        var request: ZLinkMessage? = null

        override fun find(actorId: String): CompletionStage<Optional<ActorRef>> =
            CompletableFuture.completedFuture(Optional.empty())

        override fun ensure(
            actorId: String,
            createRequest: ZLinkMessage,
            placement: ZLinkActorPlacement,
        ): CompletionStage<ActorRef> {
            this.actorId = actorId
            this.request = createRequest
            return CompletableFuture.completedFuture(actorRef)
        }
    }

    private class RecordingJoinCall<TReply>(
        private val result: ZLinkActorJoinResult<TReply>,
    ) : ZLinkActorJoinCall {
        var replyType: Class<*>? = null

        override fun timeout(timeout: Duration): ZLinkActorJoinCall = this

        override fun submit(): CompletionStage<ZLinkActorJoinResult<Void>> =
            CompletableFuture.completedFuture(ZLinkActorJoinResult.Accepted(ACTOR_REF, null))

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<ZLinkActorJoinResult<T>> {
            this.replyType = replyType
            @Suppress("UNCHECKED_CAST")
            return CompletableFuture.completedFuture(result as ZLinkActorJoinResult<T>)
        }
    }

    private class RecordingSendCall : ZLinkSendCall {
        override fun metadata(key: String, value: String): ZLinkSendCall = this

        override fun submit() = Unit
    }

    private class RecordingRequestCall<TReply>(
        private val reply: TReply,
    ) : ZLinkRequestCall {
        override fun metadata(key: String, value: String): ZLinkRequestCall = this

        override fun timeout(timeout: Duration): ZLinkRequestCall = this

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<T> =
            CompletableFuture.completedFuture(replyType.cast(reply))
    }

    private class RecordingActorClient<TReply>(
        private val reply: TReply,
    ) : ZLinkActorClient {
        var sentActorRef: ActorRef? = null
        var sentMessage: Any? = null
        var requestedActorRef: ActorRef? = null
        var requestedMessage: Any? = null

        override fun sendToActor(actorRef: ActorRef, message: Any): ZLinkActorSendCall {
            sentActorRef = actorRef
            sentMessage = message
            return RecordingActorSendCall()
        }

        override fun requestToActor(actorRef: ActorRef, request: Any): ZLinkActorRequestCall {
            requestedActorRef = actorRef
            requestedMessage = request
            return RecordingActorRequestCall(reply)
        }
    }

    private class RecordingActorSendCall : ZLinkActorSendCall {
        override fun submit() = Unit
    }

    private class RecordingActorRequestCall<TReply>(
        private val reply: TReply,
    ) : ZLinkActorRequestCall {
        override fun timeout(timeout: Duration): ZLinkActorRequestCall = this

        override fun <T : Any?> submit(replyType: Class<T>): CompletionStage<T> =
            CompletableFuture.completedFuture(replyType.cast(reply))
    }

    private class FailingRequestCall(
        private val error: Throwable,
    ) : ZLinkRequestCall {
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

        override fun onJoinedActor(actor: TestActor): CompletionStage<Void> =
            CompletableFuture.completedFuture(null)

        override fun onLeaveActor(actor: TestActor): CompletionStage<Void> =
            CompletableFuture.completedFuture(null)
    }

    companion object {
        private val NODE_RID: RoutingId = RoutingId.from(byteArrayOf(0x01))
        private val SPOT_RID: RoutingId = RoutingId.from(byteArrayOf(0x02))
        private val ACTOR_REF = ActorRef(NODE_RID, "actor-a", 7)
    }
}
