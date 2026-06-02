package systems.zlink.framework.streams;

import java.util.Map;
import java.util.Optional;

public record ZLinkStreamHeader(
    String packetName,
    Map<String, String> metadata,
    Optional<Long> requestSequence) {
}
