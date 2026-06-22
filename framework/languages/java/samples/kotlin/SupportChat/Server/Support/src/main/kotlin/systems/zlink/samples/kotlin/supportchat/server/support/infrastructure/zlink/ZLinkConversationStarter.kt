package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.SupportConversationAllocator.ConversationStartRequest
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.SupportConversationAllocator.ConversationStarter
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationCreateRequest
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.ConversationSpot

class ZLinkConversationStarter(
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : ConversationStarter {
    override suspend fun start(request: ConversationStartRequest) {
        val createPart = Message.from(
            json.writeValueAsBytes(
                ConversationCreateRequest(
                    request.customerActorId,
                    request.customerDisplayName,
                    request.subject,
                    request.createdAtUnixMs,
                ),
            ),
        )
        try {
            spots.getOrCreate(ConversationSpot::class.java, RoutingId.from(request.conversationId), createPart)
                .await()
        } finally {
            createPart.close()
        }
    }
}
