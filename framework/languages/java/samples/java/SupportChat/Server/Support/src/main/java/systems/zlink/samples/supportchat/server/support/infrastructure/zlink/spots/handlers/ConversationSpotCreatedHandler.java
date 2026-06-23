package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.ConversationSpot;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationCreateRequest;

public final class ConversationSpotCreatedHandler {
    private final ObjectMapper json;

    public ConversationSpotCreatedHandler(ObjectMapper json) {
        this.json = json;
    }

    public ZLinkSpotCreateResponse handle(ConversationSpot spot, ZLinkMessage request) {
        spot.applyCreate(decode(request));
        return ZLinkSpotCreateResponse.accept();
    }

    private ConversationCreateRequest decode(ZLinkMessage request) {
        if (request.isEmpty()) {
            throw new IllegalStateException("Conversation create request payload is required.");
        }
        return request.decode(ConversationCreateRequest.class);
    }
}
