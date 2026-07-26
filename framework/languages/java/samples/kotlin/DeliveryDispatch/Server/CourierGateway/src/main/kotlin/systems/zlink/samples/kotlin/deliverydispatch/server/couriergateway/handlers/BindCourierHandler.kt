package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.framework.actors.ActorRefSnapshot
import systems.zlink.framework.actors.ZLinkActorCreateResult
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.CourierDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq

@ZLinkHandlerGroup("courier-gateway")
class BindCourierHandler(
    private val directory: CourierDirectory,
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingRequestHandler<BindCourierReq, BindCourierRes> {
    override suspend fun handle(
        request: BindCourierReq,
        context: ZLinkRequestContext,
    ): BindCourierRes {
        val actor = when (val result = actors.getOrCreate(
            request.courierId,
            SampleNames.CourierActorType,
            EnsureCourierActorReq(request.courierId),
        ).await()) {
            is ZLinkActorCreateResult.Existing -> result.actor
            is ZLinkActorCreateResult.Created -> result.actor
            is ZLinkActorCreateResult.Rejected ->
                throw IllegalStateException("Courier Actor creation was rejected.")
        }
        val ensured = EnsureCourierActorRes(request.courierId, ActorRefSnapshot.from(actor))
        val binding = directory.remember(ensured, request.sessionRoute)
        return BindCourierRes(request.courierId, binding.actor, binding.sessionRoute)
    }
}
