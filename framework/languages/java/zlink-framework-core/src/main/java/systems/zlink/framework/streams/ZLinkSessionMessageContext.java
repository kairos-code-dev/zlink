package systems.zlink.framework.streams;

import java.util.Map;

public record ZLinkSessionMessageContext(
    String packetName,
    Map<String, String> metadata,
    boolean canReply
) {
    public ZLinkSessionMessageContext {
        if (packetName == null || packetName.isBlank()) {
            throw new IllegalArgumentException("packetName must be non-empty.");
        }
        metadata = metadata == null ? Map.of() : Map.copyOf(metadata);
    }
}
