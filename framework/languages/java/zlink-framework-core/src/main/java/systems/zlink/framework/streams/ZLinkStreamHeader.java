package systems.zlink.framework.streams;

import java.util.EnumSet;
import java.util.Map;
import java.util.Optional;

public record ZLinkStreamHeader(
    ZLinkStreamMessageKind kind,
    ZLinkStreamCodec codec,
    EnumSet<ZLinkStreamHeaderFlag> flags,
    Optional<Long> requestSequence,
    String name,
    Map<String, String> metadata) {
    public ZLinkStreamHeader {
        if (kind == null) {
            throw new IllegalArgumentException("kind is required");
        }
        if (codec == null) {
            throw new IllegalArgumentException("codec is required");
        }
        EnumSet<ZLinkStreamHeaderFlag> normalizedFlags =
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class);
        if (flags != null) {
            normalizedFlags.addAll(flags);
        }
        flags = normalizedFlags;
        requestSequence = requestSequence == null ? Optional.empty() : requestSequence;
        if (requestSequence.isPresent()) {
            flags.add(ZLinkStreamHeaderFlag.HAS_REQUEST_SEQUENCE);
        } else {
            flags.remove(ZLinkStreamHeaderFlag.HAS_REQUEST_SEQUENCE);
        }
        if (name == null || name.isBlank()) {
            throw new IllegalArgumentException("name is required");
        }
        metadata = metadata == null ? Map.of() : Map.copyOf(metadata);
        if (metadata.isEmpty()) {
            flags.remove(ZLinkStreamHeaderFlag.HAS_METADATA);
        } else {
            flags.add(ZLinkStreamHeaderFlag.HAS_METADATA);
        }
    }

    public ZLinkStreamHeader(
        String packetName,
        Map<String, String> metadata,
        Optional<Long> requestSequence) {
        this(
            requestSequence != null && requestSequence.isPresent()
                ? ZLinkStreamMessageKind.REQUEST
                : ZLinkStreamMessageKind.SEND,
            ZLinkStreamCodec.RAW,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            requestSequence,
            packetName,
            metadata);
    }

    public String packetName() {
        return name;
    }
}
