package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationCreateRequest

class ConversationSpotCreatedHandler(
    private val json: ObjectMapper,
) {
    fun handle(
        spot: ConversationSpot,
        request: Message,
    ) {
        spot.applyCreate(decode(request))
    }

    private fun decode(request: Message): ConversationCreateRequest {
        check(!request.isEmpty()) { "Conversation create request payload is required." }
        return json.readValue(request.toByteArray(), ConversationCreateRequest::class.java)
    }
}
