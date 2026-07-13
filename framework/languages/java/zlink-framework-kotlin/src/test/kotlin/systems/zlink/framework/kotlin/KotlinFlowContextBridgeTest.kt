package systems.zlink.framework.kotlin

import java.util.Optional
import kotlinx.coroutines.yield
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Test
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorJoinCall
import systems.zlink.framework.actors.ZLinkBoundSession
import systems.zlink.framework.configuration.ZLinkFlowOrigin
import systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext

class KotlinFlowContextBridgeTest {
    @Test
    fun `suspending lifecycle preserves flow across suspension and clears it afterward`() {
        val flow = ZLinkFlowContext.create(ZLinkFlowOrigin.LIFECYCLE)
        val first = RecordingActorFactory()
        val firstStage = ZLinkFlowContext.enter(flow).use {
            first.create("actor-a", UNUSED_CONTEXT)
        }

        firstStage.toCompletableFuture().join()

        assertEquals(flow, first.beforeSuspension)
        assertEquals(flow, first.afterSuspension)

        val second = RecordingActorFactory()
        second.create("actor-b", UNUSED_CONTEXT).toCompletableFuture().join()
        assertNull(second.beforeSuspension)
        assertNull(second.afterSuspension)
    }

    private class RecordingActorFactory : ZLinkSuspendingActorFactory() {
        var beforeSuspension: ZLinkFlowContext.State? = null
        var afterSuspension: ZLinkFlowContext.State? = null

        override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor {
            beforeSuspension = ZLinkFlowContext.current()
            yield()
            afterSuspension = ZLinkFlowContext.current()
            return object : ZLinkActor {
                override fun actorId() = actorId
                override fun context() = context
            }
        }
    }

    companion object {
        private val UNUSED_CONTEXT = object : ZLinkActorContext {
            override fun spotRid(): Optional<RoutingId> = Optional.empty()
            override fun boundSession(): ZLinkBoundSession = error("not used")
            override fun joinSpot(spotRid: RoutingId, request: Any): ZLinkActorJoinCall = error("not used")
            override fun joinEntrySpot(spotNodeRid: RoutingId, request: Any): ZLinkActorJoinCall = error("not used")
        }
    }
}
