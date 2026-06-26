package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.entryspot

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.actors.CustomerActor

class CustomerEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot<CustomerActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
    }

    override fun onActorJoin(
        actor: CustomerActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept()
}
