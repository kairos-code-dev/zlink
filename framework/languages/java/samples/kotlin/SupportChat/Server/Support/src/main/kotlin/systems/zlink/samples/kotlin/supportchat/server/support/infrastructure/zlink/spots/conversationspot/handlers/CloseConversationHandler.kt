package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationRes

class CloseConversationHandler : ZLinkSuspendingSpotActorRequestHandler<
    ConversationSpot,
    SupportUserActor,
    CloseConversationReq,
    CloseConversationRes,
    > {
    override suspend fun handle(
        spot: ConversationSpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: CloseConversationReq,
    ): CloseConversationRes = spot.close(actor, request)
}
