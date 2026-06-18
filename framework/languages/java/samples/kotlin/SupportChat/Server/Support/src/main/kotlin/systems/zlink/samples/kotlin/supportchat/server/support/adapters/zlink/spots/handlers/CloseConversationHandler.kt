package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationRes

class CloseConversationHandler() : ZLinkSuspendingSpotActorRequestHandler<
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
        cancellationToken: CancellationToken,
    ): CloseConversationRes =
        spot.close(actor, request)
}
