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
    Map<String, String> metadata,
    // First-class correlation id (flag 0x08, wire layout: after metadata, u8 length +
    // UTF-8 bytes). Client-generated, server-echoed. Empty = absent.
    Optional<String> correlationId) {
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
        correlationId = correlationId == null ? Optional.empty() : correlationId;
        if (correlationId.isPresent() && !correlationId.get().isEmpty()) {
            flags.add(ZLinkStreamHeaderFlag.HAS_CORRELATION_ID);
        } else {
            correlationId = Optional.empty();
            flags.remove(ZLinkStreamHeaderFlag.HAS_CORRELATION_ID);
        }
    }

    // Back-compat 6-arg constructor (no correlation id).
    public ZLinkStreamHeader(
        ZLinkStreamMessageKind kind,
        ZLinkStreamCodec codec,
        EnumSet<ZLinkStreamHeaderFlag> flags,
        Optional<Long> requestSequence,
        String name,
        Map<String, String> metadata) {
        this(kind, codec, flags, requestSequence, name, metadata, Optional.empty());
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
            metadata,
            Optional.empty());
    }

    public String packetName() {
        return name;
    }

    // Returns a copy of this header carrying the given correlation id (for echoing the
    // request corr onto a reply, or stamping a generated corr on an outbound packet).
    public ZLinkStreamHeader withCorrelationId(String correlationId) {
        return new ZLinkStreamHeader(
            kind, codec, flags, requestSequence, name, metadata,
            correlationId == null ? Optional.empty() : Optional.of(correlationId));
    }
}
