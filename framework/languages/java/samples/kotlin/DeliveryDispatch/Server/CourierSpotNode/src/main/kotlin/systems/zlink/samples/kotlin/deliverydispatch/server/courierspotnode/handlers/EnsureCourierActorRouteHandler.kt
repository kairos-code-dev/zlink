package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRefSnapshot
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq

@ZLinkHandlerGroup("courier-actor-node")
class EnsureCourierActorRouteHandler(
    private val actors: ZLinkActorManager,
) : ZLinkRequestHandler<EnsureCourierActorReq, EnsureCourierActorRes> {
    override fun handle(
        request: EnsureCourierActorReq,
        context: ZLinkRequestContext,
    ): EnsureCourierActorRes {
        val actor = ZLinkAwait.await(actors.getOrCreate(request.courierId, SampleNames.CourierActorType, request))
        return EnsureCourierActorRes(
            courierId = request.courierId,
            actor = ZLinkActorRefSnapshot.from(actor),
        )
    }
}
