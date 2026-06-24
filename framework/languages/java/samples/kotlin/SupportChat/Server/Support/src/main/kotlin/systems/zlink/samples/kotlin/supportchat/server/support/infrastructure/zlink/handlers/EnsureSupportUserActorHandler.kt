package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ActorRefSnapshot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorRes

@ZLinkHandlerGroup("support")
class EnsureSupportUserActorHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes> {
    override suspend fun handle(
        request: EnsureSupportUserActorReq,
        context: ZLinkRequestContext,
    ) = run {
        val actor = actors.getOrCreate(request.actorId, SampleNames.SupportActorType, request).await()
        EnsureSupportUserActorRes(toSnapshot(actor))
    }

    private fun toSnapshot(actor: ZLinkActorRef): ActorRefSnapshot =
        ActorRefSnapshot(actor.nodeRid().toBytes(), actor.actorId(), actor.epoch())
}
