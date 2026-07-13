package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageRes

class SendChatMessageHandler : ZLinkSuspendingSpotActorRequestHandler<
    ConversationSpot,
    SupportUserActor,
    SendChatMessageReq,
    SendChatMessageRes,
    > {
    override suspend fun handle(
        spot: ConversationSpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: SendChatMessageReq,
    ): SendChatMessageRes = spot.sendMessage(actor, request)
}
