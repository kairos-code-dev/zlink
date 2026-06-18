package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingRes

class SetTypingHandler() : ZLinkSuspendingSpotActorRequestHandler<
    ConversationSpot,
    SupportUserActor,
    SetTypingReq,
    SetTypingRes,
    > {
    override suspend fun handle(
        spot: ConversationSpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: SetTypingReq,
        cancellationToken: CancellationToken,
    ): SetTypingRes =
        spot.setTyping(actor, request)
}
