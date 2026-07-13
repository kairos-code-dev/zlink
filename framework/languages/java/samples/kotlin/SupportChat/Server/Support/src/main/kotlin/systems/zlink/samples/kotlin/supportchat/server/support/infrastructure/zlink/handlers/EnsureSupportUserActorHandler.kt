package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.actors.ActorRefSnapshot
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorRes

@ZLinkHandlerGroup(SampleNames.SupportChannel)
class EnsureSupportUserActorHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes> {
    override suspend fun handle(
        request: EnsureSupportUserActorReq,
        context: ZLinkRequestContext,
    ): EnsureSupportUserActorRes {
        val existing = actors.find(request.actorId).await().orElse(null)
        if (existing != null) {
            return existing.toResponse()
        }
        val actor = actors
            .getOrCreate(request.actorId, SampleNames.SupportActorType, request)
            .await()
        return actor.toResponse()
    }
}

private fun ActorRef.toResponse(): EnsureSupportUserActorRes =
    EnsureSupportUserActorRes(
        actor = ActorRefSnapshot.from(this),
    )
