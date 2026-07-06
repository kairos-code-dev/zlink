package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.ActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.CourierActor
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers.EnsureCourierActorRouteHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers.FindCourierActorRouteHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers.OfferDeliveryRouteHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.handlers.BindCourierSessionActorHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.handlers.CourierDecisionActorHandler

class CourierEntrySpot(
    private val entryContext: ZLinkEntrySpotContext,
    private val actors: ActorDirectory,
) : ZLinkEntrySpot<CourierActor> {
    override fun context(): ZLinkEntrySpotContext = entryContext

    override fun configure() {
        entryContext.handlers().addHandler(FindCourierActorRouteHandler::class.java)
        entryContext.handlers().addHandler(EnsureCourierActorRouteHandler::class.java)
        entryContext.handlers().addHandler(OfferDeliveryRouteHandler::class.java)
        entryContext.handlers().addHandler(BindCourierSessionActorHandler::class.java)
        entryContext.handlers().addHandler(CourierDecisionActorHandler::class.java)
    }

    override fun onCreateActor(
        actor: CourierActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken,
    ) {
        actors.register(actor)
    }

    override fun onActorJoin(
        actor: CourierActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse = ZLinkSpotActorJoinResponse.accept()
}
