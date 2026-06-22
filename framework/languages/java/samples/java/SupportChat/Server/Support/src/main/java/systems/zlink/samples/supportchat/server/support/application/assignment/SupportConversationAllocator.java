package systems.zlink.samples.supportchat.server.support.application.assignment;

import java.util.concurrent.atomic.AtomicInteger;

public final class SupportConversationAllocator {
    private final ConversationStarter conversations;
    private final AtomicInteger conversationSeq = new AtomicInteger();

    public SupportConversationAllocator(ConversationStarter conversations) {
        this.conversations = conversations;
    }

    public String allocate(String customerActorId, String customerDisplayName, String subject) {
        if (customerActorId == null || customerActorId.isBlank()) {
            throw new IllegalStateException("customerActorId is required");
        }
        String conversationId = "conversation-%03d".formatted(conversationSeq.incrementAndGet());
        conversations.start(new ConversationStartRequest(
            conversationId,
            customerActorId,
            customerDisplayName,
            subject,
            System.currentTimeMillis()));
        return conversationId;
    }

    public record ConversationStartRequest(
        String conversationId,
        String customerActorId,
        String customerDisplayName,
        String subject,
        long createdAtUnixMs) {
    }

    public interface ConversationStarter {
        void start(ConversationStartRequest request);
    }
}
