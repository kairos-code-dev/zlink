package systems.zlink.samples.supportchat.server.support.application.assignment;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.concurrent.atomic.AtomicInteger;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.ConversationSpot;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationCreateRequest;

public final class SupportConversationAllocator {
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;
    private final AtomicInteger conversationSeq = new AtomicInteger();

    public SupportConversationAllocator(ZLinkSpotManager spots, ObjectMapper json) {
        this.spots = spots;
        this.json = json;
    }

    public String allocate(String customerActorId, String customerDisplayName, String subject) {
        if (customerActorId == null || customerActorId.isBlank()) {
            throw new IllegalStateException("customerActorId is required");
        }
        String conversationId = "conversation-%03d".formatted(conversationSeq.incrementAndGet());
        Message createPart = serialize(new ConversationCreateRequest(
            customerActorId,
            customerDisplayName,
            subject,
            System.currentTimeMillis()));
        try {
            await(spots.getOrCreate(ConversationSpot.class, RoutingId.from(conversationId), createPart));
            return conversationId;
        } finally {
            createPart.close();
        }
    }

    private Message serialize(ConversationCreateRequest request) {
        try {
            return Message.from(json.writeValueAsBytes(request));
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Failed to encode conversation create request.", ex);
        }
    }
}
