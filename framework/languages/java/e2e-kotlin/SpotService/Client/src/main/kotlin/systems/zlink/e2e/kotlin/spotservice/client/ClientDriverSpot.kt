package systems.zlink.e2e.kotlin.spotservice.client

import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class ClientDriverSpot(
    private val context: ZLinkSpotContext,
    private val routes: ZLinkRouteClient
) : ZLinkSpot<ZLinkActor> {
    override fun context(): ZLinkSpotContext = context

    override fun onInitialize() {
        try {
            ClientScenario(context.outbound(), routes).runMode(mode)
            result.complete(null)
        } catch (error: Throwable) {
            result.completeExceptionally(error)
        }
    }

    companion object {
        private var result = CompletableFuture<Void>()
        private var mode = "state1"

        @JvmStatic
        fun configure(nextMode: String) {
            mode = nextMode
            result = CompletableFuture()
        }

        @JvmStatic
        fun awaitResult() {
            try {
                result.get(45, TimeUnit.SECONDS)
            } catch (error: Exception) {
                throw IllegalStateException("client driver scenario failed", error)
            }
        }
    }
}
