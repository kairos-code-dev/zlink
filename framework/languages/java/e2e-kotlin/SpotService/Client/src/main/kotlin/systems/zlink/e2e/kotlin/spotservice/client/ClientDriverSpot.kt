package systems.zlink.e2e.kotlin.spotservice.client

import systems.zlink.framework.CancellationToken
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import systems.zlink.e2e.kotlin.spotservice.client.support.SpotHttpDriver
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class ClientDriverSpot(
    private val context: ZLinkSpotContext,
    private val routes: ZLinkRouteClient
) : ZLinkSpot<ZLinkActor> {
    override fun onJoinedActor(actor: ZLinkActor, cancellationToken: CancellationToken) {
    }

    override fun onLeaveActor(actor: ZLinkActor, cancellationToken: CancellationToken) {
    }
    private var mode: String = nextMode

    override fun context(): ZLinkSpotContext = context

    override fun onCreate(request: ZLinkMessage) =
        systems.zlink.framework.spots.ZLinkSpotCreateResponse.accept().also {
            mode = if (request.isEmpty) mode else request.decode(String::class.java)
    }

    override fun onInitialize() {
        val driver = SpotHttpDriver(outbound = context.outbound(), routes = routes)
        Thread {
            try {
                ClientScenario(driver).runMode(mode)
                println("spot-service kotlin e2e mode=$mode result=passed")
                result.complete(null)
            } catch (error: Throwable) {
                result.completeExceptionally(error)
            }
        }.apply {
            name = "zlink-kotlin-client-driver-$mode"
            isDaemon = true
            start()
        }
    }

    companion object {
        private var nextMode: String = "route-mesh"
        private var result: CompletableFuture<Void> = CompletableFuture()

        fun configure(mode: String) {
            nextMode = mode
            result = CompletableFuture()
        }

        fun awaitResult() {
            try {
                result.get(60, TimeUnit.SECONDS)
            } catch (error: Exception) {
                throw IllegalStateException("client driver scenario failed", error)
            }
        }
    }
}
