package systems.zlink.framework.runtime.spots;

import java.util.Map;
import java.util.Optional;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

record ZLinkSpotActorSendHandlerContext(
    String packet,
    Map<String, String> metadata) implements ZLinkSpotActorSendContext {
    ZLinkSpotActorSendHandlerContext {
        metadata = metadata == null ? Map.of() : Map.copyOf(metadata);
    }

    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.ofNullable(packet); }
    @Override public Optional<String> contentType() { return Optional.empty(); }
}

record ZLinkSpotActorRequestHandlerContext(
    String packet,
    Map<String, String> metadata) implements ZLinkSpotActorRequestContext {
    ZLinkSpotActorRequestHandlerContext {
        metadata = metadata == null ? Map.of() : Map.copyOf(metadata);
    }

    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.ofNullable(packet); }
    @Override public Optional<String> contentType() { return Optional.empty(); }
}
