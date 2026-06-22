package systems.zlink.samples.supportchat.server.support.infrastructure.zlink;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.supportchat.server.support.application.assignment.SupportConversationAllocator.ConversationStartRequest;
import systems.zlink.samples.supportchat.server.support.application.assignment.SupportConversationAllocator.ConversationStarter;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationCreateRequest;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.ConversationSpot;

public final class ZLinkConversationStarter implements ConversationStarter {
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;

    public ZLinkConversationStarter(ZLinkSpotManager spots, ObjectMapper json) {
        this.spots = spots;
        this.json = json;
    }

    @Override
    public void start(ConversationStartRequest request) {
        Message createPart = serialize(new ConversationCreateRequest(
            request.customerActorId(),
            request.customerDisplayName(),
            request.subject(),
            request.createdAtUnixMs()));
        try {
            await(spots.getOrCreate(ConversationSpot.class, RoutingId.from(request.conversationId()), createPart));
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
