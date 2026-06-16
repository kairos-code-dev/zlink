package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationRes

class CloseConversationHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotActorRequestHandler<
    ConversationSpot,
    SupportUserActor,
    CloseConversationReq,
    CloseConversationRes,
    >(coroutines) {
    override suspend fun handleSuspending(
        spot: ConversationSpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: CloseConversationReq,
        cancellationToken: CancellationToken,
    ): CloseConversationRes =
        spot.close(actor, request)
}
