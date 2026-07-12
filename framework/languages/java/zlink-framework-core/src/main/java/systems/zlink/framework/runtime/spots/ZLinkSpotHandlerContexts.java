package systems.zlink.framework.runtime.spots;

import java.util.Optional;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;

record ZLinkSpotActorSendHandlerContext(String packet) implements ZLinkSpotActorSendContext {
    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.ofNullable(packet); }
    @Override public Optional<String> contentType() { return Optional.empty(); }
}

record ZLinkSpotActorRequestHandlerContext(String packet) implements ZLinkSpotActorRequestContext {
    @Override public Optional<String> channelName() { return Optional.empty(); }
    @Override public Optional<String> packetName() { return Optional.ofNullable(packet); }
    @Override public Optional<String> contentType() { return Optional.empty(); }
}
