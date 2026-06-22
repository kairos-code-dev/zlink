package systems.zlink.samples.kotlin.supportchat.server.support.application.assignment

import java.util.concurrent.atomic.AtomicInteger

class SupportConversationAllocator(
    private val conversations: ConversationStarter,
) {
    private val conversationSeq = AtomicInteger()

    suspend fun allocate(
        customerActorId: String,
        customerDisplayName: String,
        subject: String,
    ): String {
        check(customerActorId.isNotBlank()) { "customerActorId is required" }
        val conversationId = "conversation-%03d".format(conversationSeq.incrementAndGet())
        conversations.start(
            ConversationStartRequest(
                conversationId = conversationId,
                customerActorId = customerActorId,
                customerDisplayName = customerDisplayName,
                subject = subject,
                createdAtUnixMs = System.currentTimeMillis(),
            ),
        )
        return conversationId
    }

    data class ConversationStartRequest(
        val conversationId: String,
        val customerActorId: String,
        val customerDisplayName: String,
        val subject: String,
        val createdAtUnixMs: Long,
    )

    interface ConversationStarter {
        suspend fun start(request: ConversationStartRequest)
    }
}
