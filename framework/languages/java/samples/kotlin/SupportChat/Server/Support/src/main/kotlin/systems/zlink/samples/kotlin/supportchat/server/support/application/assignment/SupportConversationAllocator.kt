package systems.zlink.samples.kotlin.supportchat.server.support.application.assignment

import com.fasterxml.jackson.databind.ObjectMapper
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationCreateRequest

class SupportConversationAllocator(
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) {
    private val conversationSeq = AtomicInteger()

    suspend fun allocate(
        customerActorId: String,
        customerDisplayName: String,
        subject: String,
    ): String {
        check(customerActorId.isNotBlank()) { "customerActorId is required" }
        val conversationId = "conversation-%03d".format(conversationSeq.incrementAndGet())
        val createPart = Message.from(
            json.writeValueAsBytes(
                ConversationCreateRequest(
                    customerActorId,
                    customerDisplayName,
                    subject,
                    System.currentTimeMillis(),
                ),
            ),
        )
        return try {
            spots.getOrCreate(ConversationSpot::class.java, RoutingId.from(conversationId), createPart)
                .await()
            conversationId
        } finally {
            createPart.close()
        }
    }
}
