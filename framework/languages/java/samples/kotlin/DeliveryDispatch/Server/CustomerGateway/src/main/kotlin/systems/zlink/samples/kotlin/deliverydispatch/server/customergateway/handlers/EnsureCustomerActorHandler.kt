package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.handlers

import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.actors.ActorRefSnapshot
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorReq

@ZLinkHandlerGroup("customer-route")
class EnsureCustomerActorHandler(
    private val actors: ZLinkActorManager,
) : ZLinkRequestHandler<EnsureCustomerActorReq, EnsureCustomerActorRes> {
    override fun handle(
        request: EnsureCustomerActorReq,
        context: ZLinkRequestContext,
    ): EnsureCustomerActorRes {
        val actor = await(actors.getOrCreate(request.customerId, SampleNames.CustomerActorType, request))
        return EnsureCustomerActorRes(
            customerId = request.customerId,
            actor = ActorRefSnapshot.from(actor),
        )
    }
}
