package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingRes

class SetTypingHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotActorRequestHandler<
    ConversationSpot,
    SupportUserActor,
    SetTypingReq,
    SetTypingRes,
    >(coroutines) {
    override suspend fun handleSuspending(
        spot: ConversationSpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: SetTypingReq,
        cancellationToken: CancellationToken,
    ): SetTypingRes =
        spot.setTyping(actor, request)
}
