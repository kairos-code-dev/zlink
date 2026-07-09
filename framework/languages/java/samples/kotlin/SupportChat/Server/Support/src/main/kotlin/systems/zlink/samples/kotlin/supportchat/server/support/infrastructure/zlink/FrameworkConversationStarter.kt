package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.supportchat.server.support.application.ConversationStartReq
import systems.zlink.samples.kotlin.supportchat.server.support.application.ConversationStarter
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationCreateReq
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot

class FrameworkConversationStarter(
    private val spots: ZLinkSpotManager,
) : ConversationStarter {
    override suspend fun start(
        conversationId: String,
        request: ConversationStartReq,
    ) {
        spots.getOrCreate(
            ConversationSpot::class.java,
            RoutingId.from(conversationId),
            ZLinkMessage.of(
                ConversationCreateReq(
                    customerActorId = request.customerActorId,
                    customerDisplayName = request.customerDisplayName,
                    subject = request.subject,
                    createdAtUnixMs = request.createdAtUnixMs,
                ),
            ),
        ).await()
    }
}
