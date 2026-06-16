package systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.ConversationSpot;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationCreateRequest;

public final class ConversationSpotCreatedHandler {
    private final ObjectMapper json;

    public ConversationSpotCreatedHandler(ObjectMapper json) {
        this.json = json;
    }

    public ZLinkSpotCreateResponse handle(ConversationSpot spot, Message request) {
        spot.applyCreate(decode(request));
        return ZLinkSpotCreateResponse.accept();
    }

    private ConversationCreateRequest decode(Message request) {
        if (request.isEmpty()) {
            throw new IllegalStateException("Conversation create request payload is required.");
        }
        try {
            return json.readValue(request.toByteArray(), ConversationCreateRequest.class);
        } catch (IOException ex) {
            throw new IllegalStateException("Failed to decode conversation create request.", ex);
        }
    }
}
