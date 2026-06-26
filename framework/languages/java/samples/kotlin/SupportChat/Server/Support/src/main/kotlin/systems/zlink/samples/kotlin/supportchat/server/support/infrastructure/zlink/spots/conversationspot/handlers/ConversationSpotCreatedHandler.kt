package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationCreateRequest

class ConversationSpotCreatedHandler(
    private val json: ObjectMapper,
) {
    fun handle(
        spot: ConversationSpot,
        request: ZLinkMessage,
    ) {
        spot.applyCreate(decode(request))
    }

    private fun decode(request: ZLinkMessage): ConversationCreateRequest {
        check(!request.isEmpty()) { "Conversation create request payload is required." }
        return request.decode(ConversationCreateRequest::class.java)
    }
}
