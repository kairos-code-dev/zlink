package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageRes

class SendChatMessageHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotActorRequestHandler<
    ConversationSpot,
    SupportUserActor,
    SendChatMessageReq,
    SendChatMessageRes,
    >(coroutines) {
    override suspend fun handleSuspending(
        spot: ConversationSpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: SendChatMessageReq,
        cancellationToken: CancellationToken,
    ): SendChatMessageRes =
        spot.sendMessage(actor, request)
}
