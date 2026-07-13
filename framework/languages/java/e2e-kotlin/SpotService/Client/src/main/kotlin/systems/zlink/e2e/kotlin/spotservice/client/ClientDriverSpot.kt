package systems.zlink.e2e.kotlin.spotservice.client

import systems.zlink.framework.kotlin.*

import java.util.concurrent.CompletableFuture
import systems.zlink.e2e.kotlin.spotservice.client.support.SpotHttpDriver
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.SpotHandleResolver

class ClientDriverSpot(
    private val context: ZLinkSpotContext,
    private val routes: ZLinkRouteClient,
    private val spotHandles: SpotHandleResolver,
) : ZLinkSuspendingSpot<ZLinkActor>() {
    private var mode: String = nextMode

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: ZLinkMessage) =
        systems.zlink.framework.spots.ZLinkSpotCreateResponse.accept().also {
            mode = if (request.isEmpty) mode else request.decode(String::class.java)
    }

    override suspend fun onInitializeSuspending() {
        val driver = SpotHttpDriver(outbound = context.outbound(), routes = routes, spotHandles = spotHandles)
        try {
            ClientScenario(driver).runMode(mode)
            println("spot-service kotlin e2e mode=$mode result=passed")
            result.complete(null)
        } catch (error: Throwable) {
            result.completeExceptionally(error)
        }
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse = ZLinkSpotActorJoinResponse.accept()

    override suspend fun onJoinedActorSuspending(actor: ZLinkActor) {
    }

    override suspend fun onLeaveActorSuspending(actor: ZLinkActor) {
    }

    companion object {
        private var nextMode: String = "route-mesh"
        private var result: CompletableFuture<Void> = CompletableFuture()

        fun configure(mode: String): CompletableFuture<Void> {
            nextMode = mode
            result = CompletableFuture()
            return result
        }
    }
}
