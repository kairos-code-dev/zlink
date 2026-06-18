package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ActorRefSnapshot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationRes

@ZLinkHandlerGroup("support")
class EnsureSupportUserActorHandler(
    private val actors: ZLinkActorManager,
    private val directory: SupportActorDirectory,
) : ZLinkSuspendingRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes> {
    override suspend fun handle(
        request: EnsureSupportUserActorReq,
        context: ZLinkRequestContext,
    ) = run {
        val actor = actors.getOrCreate(request.actorId, SampleNames.SupportActorType).await()
        val supportActor = actor as? SupportUserActor
            ?: throw IllegalStateException("Support actor factory returned an unexpected actor type.")
        supportActor.setIdentity(request.displayName, request.role)
        directory.addOrUpdate(supportActor)
        val joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.SupportRid), Message.from(ByteArray(0)))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Message::class.java)
            .await()
        if (supportActor.conversationId.isNotBlank()) {
            actor.context()
                .joinSpot(
                    RoutingId.from(supportActor.conversationId),
                    JoinConversationReq(supportActor.conversationId),
                )
                .timeout(SampleTimings.RequestTimeout)
                .submit(JoinConversationRes::class.java)
                .await()
        }
        EnsureSupportUserActorRes(toSnapshot(joined.actor()))
    }

    private fun toSnapshot(actor: ZLinkActorRef): ActorRefSnapshot =
        ActorRefSnapshot(actor.nodeRid().toBytes(), actor.actorId(), actor.epoch())
}
