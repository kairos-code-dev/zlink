package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot.SupportEntrySpot
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.Roles
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationApiReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationApiRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationRes

class OpenConversationActorHandler() : ZLinkSuspendingEntrySpotActorRequestHandler<
    SupportEntrySpot,
    SupportUserActor,
    OpenConversationReq,
    OpenConversationRes,
    > {
    override suspend fun handle(
        entrySpot: SupportEntrySpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: OpenConversationReq,
        cancellationToken: CancellationToken,
    ): OpenConversationRes {
        check(Roles.Customer == actor.role) { "Only customer actors can open a conversation." }
        val opened = entrySpot.context().outbound().requestToChannel(
            SampleNames.ApiChannel,
            OpenConversationApiReq(actor.actorId(), actor.displayName, request.subject),
        )
            .timeout(SampleTimings.RequestTimeout)
            .submit(OpenConversationApiRes::class.java)
            .await()
        if (cancellationToken.isCancellationRequested) {
            throw IllegalStateException("OpenConversationReq was cancelled")
        }
        val joined = actor.context()
            .joinSpot(
                RoutingId.from(opened.conversationId),
                JoinConversationReq(opened.conversationId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(JoinConversationRes::class.java)
            .await()
        return OpenConversationRes(opened.conversationId, joined.reply().state)
    }
}
