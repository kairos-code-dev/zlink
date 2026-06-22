package systems.zlink.framework.runtime.streams;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.EnumSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkStreamHeaderCodec {
    static final int KIND_SEND = 1;
    static final int KIND_REQUEST = 2;
    static final int KIND_RESPONSE = 3;
    private static final int FLAG_HAS_REQUEST_SEQ = 0x01;

    private ZLinkStreamHeaderCodec() {
    }

    public static ZLinkStreamHeader decodeOrPlain(byte[] bytes) {
        if (bytes.length < 4 || !isKnownKind(bytes[0])) {
            return new ZLinkStreamHeader(
                new String(bytes, StandardCharsets.UTF_8),
                Map.of(),
                Optional.empty());
        }
        int kind = Byte.toUnsignedInt(bytes[0]);
        int codec = Byte.toUnsignedInt(bytes[1]);
        int flags = Byte.toUnsignedInt(bytes[2]);
        int offset = 3;
        Optional<Long> requestSeq = Optional.empty();
        if ((flags & FLAG_HAS_REQUEST_SEQ) != 0) {
            if (bytes.length - offset < Long.BYTES) {
                throw new IllegalArgumentException("STREAM header request sequence is incomplete");
            }
            long value = ByteBuffer.wrap(bytes, offset, Long.BYTES).getLong();
            if (value == 0) {
                throw new IllegalArgumentException("STREAM request sequence must not be zero");
            }
            requestSeq = Optional.of(value);
            offset += Long.BYTES;
        }
        if (bytes.length - offset < 1) {
            throw new IllegalArgumentException("STREAM header packet name length is missing");
        }
        int nameLength = Byte.toUnsignedInt(bytes[offset++]);
        if (nameLength == 0 || bytes.length - offset < nameLength) {
            throw new IllegalArgumentException("STREAM header packet name is invalid");
        }
        String packetName = new String(bytes, offset, nameLength, StandardCharsets.UTF_8);
        offset += nameLength;
        Map<String, String> metadata = Map.of();
        if ((flags & ZLinkStreamHeaderFlag.HAS_METADATA.value()) != 0) {
            if (bytes.length - offset < 2) {
                throw new IllegalArgumentException("STREAM header metadata is incomplete");
            }
            int metadataLength = Short.toUnsignedInt(ByteBuffer.wrap(bytes, offset, 2).getShort());
            offset += 2;
            if (bytes.length - offset < metadataLength) {
                throw new IllegalArgumentException("STREAM header metadata is incomplete");
            }
            metadata = decodeMetadata(bytes, offset, metadataLength);
            offset += metadataLength;
        }
        Optional<String> correlationId = Optional.empty();
        if ((flags & ZLinkStreamHeaderFlag.HAS_CORRELATION_ID.value()) != 0) {
            if (bytes.length - offset < 1) {
                throw new IllegalArgumentException("STREAM header correlation id length is missing");
            }
            int corrLength = Byte.toUnsignedInt(bytes[offset++]);
            if (corrLength == 0 || bytes.length - offset < corrLength) {
                throw new IllegalArgumentException("STREAM header correlation id is invalid");
            }
            correlationId = Optional.of(new String(bytes, offset, corrLength, StandardCharsets.UTF_8));
            offset += corrLength;
        }
        if (offset != bytes.length) {
            throw new IllegalArgumentException("STREAM header contains trailing bytes");
        }
        return new ZLinkStreamHeader(
            ZLinkStreamMessageKind.fromValue(kind),
            ZLinkStreamCodec.fromValue(codec),
            flagsFromValue(flags),
            requestSeq,
            packetName,
            metadata,
            correlationId);
    }

    public static byte[] encode(ZLinkStreamHeader header) {
        if (header == null) {
            throw new IllegalArgumentException("header is required");
        }
        return encode(
            header.kind().value(),
            header.codec().value(),
            flagsValue(header.flags()),
            header.packetName(),
            header.requestSequence(),
            header.metadata(),
            header.correlationId());
    }

    static byte[] encode(int kind, String packetName, Optional<Long> requestSeq) {
        return encode(kind, 0, 0, packetName, requestSeq, Map.of(), Optional.empty());
    }

    private static byte[] encode(
        int kind,
        int codec,
        int initialFlags,
        String packetName,
        Optional<Long> requestSeq,
        Map<String, String> metadata,
        Optional<String> correlationId) {
        if (packetName == null || packetName.isBlank()) {
            throw new IllegalArgumentException("packetName is required");
        }
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        byte[] metadataBytes = encodeMetadata(metadata);
        boolean hasMetadata = metadataBytes.length > 0;
        boolean hasCorrelationId = correlationId != null
            && correlationId.isPresent()
            && !correlationId.get().isEmpty();
        byte[] correlationBytes = hasCorrelationId
            ? correlationId.get().getBytes(StandardCharsets.UTF_8)
            : new byte[0];
        if (correlationBytes.length > 255) {
            throw new IllegalArgumentException("STREAM correlation id is too long");
        }
        int flags = requestSeq.isPresent()
            ? initialFlags | FLAG_HAS_REQUEST_SEQ
            : initialFlags & ~FLAG_HAS_REQUEST_SEQ;
        flags = hasMetadata
            ? flags | ZLinkStreamHeaderFlag.HAS_METADATA.value()
            : flags & ~ZLinkStreamHeaderFlag.HAS_METADATA.value();
        flags = hasCorrelationId
            ? flags | ZLinkStreamHeaderFlag.HAS_CORRELATION_ID.value()
            : flags & ~ZLinkStreamHeaderFlag.HAS_CORRELATION_ID.value();
        ByteBuffer buffer = ByteBuffer.allocate(
            3
                + (requestSeq.isPresent() ? Long.BYTES : 0)
                + 1
                + name.length
                + (hasMetadata ? 2 + metadataBytes.length : 0)
                + (hasCorrelationId ? 1 + correlationBytes.length : 0));
        buffer.put((byte) kind);
        buffer.put((byte) codec);
        buffer.put((byte) flags);
        requestSeq.ifPresent(value -> {
            if (value == 0) {
                throw new IllegalArgumentException("STREAM request sequence must not be zero");
            }
            buffer.putLong(value);
        });
        buffer.put((byte) name.length);
        buffer.put(name);
        if (hasMetadata) {
            buffer.putShort((short) metadataBytes.length);
            buffer.put(metadataBytes);
        }
        if (hasCorrelationId) {
            buffer.put((byte) correlationBytes.length);
            buffer.put(correlationBytes);
        }
        return buffer.array();
    }

    private static boolean isKnownKind(byte value) {
        int kind = Byte.toUnsignedInt(value);
        return kind >= KIND_SEND && kind <= 5;
    }

    private static EnumSet<ZLinkStreamHeaderFlag> flagsFromValue(int value) {
        EnumSet<ZLinkStreamHeaderFlag> flags =
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class);
        for (ZLinkStreamHeaderFlag flag : ZLinkStreamHeaderFlag.values()) {
            if ((value & flag.value()) != 0) {
                flags.add(flag);
                value &= ~flag.value();
            }
        }
        if (value != 0) {
            throw new IllegalArgumentException("STREAM header contains unknown flags");
        }
        return flags;
    }

    private static int flagsValue(EnumSet<ZLinkStreamHeaderFlag> flags) {
        int value = 0;
        for (ZLinkStreamHeaderFlag flag : flags) {
            value |= flag.value();
        }
        return value;
    }

    private static byte[] encodeMetadata(Map<String, String> metadata) {
        if (metadata == null || metadata.isEmpty()) {
            return new byte[0];
        }
        if (metadata.size() > 255) {
            throw new IllegalArgumentException("STREAM metadata entry count must not exceed 255");
        }
        int size = 1;
        for (Map.Entry<String, String> entry : metadata.entrySet()) {
            byte[] key = entry.getKey().getBytes(StandardCharsets.UTF_8);
            byte[] value = entry.getValue().getBytes(StandardCharsets.UTF_8);
            if (key.length == 0 || key.length > 255) {
                throw new IllegalArgumentException("STREAM metadata key length is invalid");
            }
            if (value.length > 65535) {
                throw new IllegalArgumentException("STREAM metadata value is too large");
            }
            size += 1 + key.length + 2 + value.length;
        }
        ByteBuffer buffer = ByteBuffer.allocate(size);
        buffer.put((byte) metadata.size());
        for (Map.Entry<String, String> entry : metadata.entrySet()) {
            byte[] key = entry.getKey().getBytes(StandardCharsets.UTF_8);
            byte[] value = entry.getValue().getBytes(StandardCharsets.UTF_8);
            buffer.put((byte) key.length);
            buffer.put(key);
            buffer.putShort((short) value.length);
            buffer.put(value);
        }
        return buffer.array();
    }

    private static Map<String, String> decodeMetadata(byte[] bytes, int offset, int length) {
        if (length == 0) {
            throw new IllegalArgumentException("STREAM metadata payload is empty");
        }
        ByteBuffer buffer = ByteBuffer.wrap(bytes, offset, length);
        int count = Byte.toUnsignedInt(buffer.get());
        Map<String, String> metadata = new LinkedHashMap<>();
        for (int i = 0; i < count; i++) {
            if (buffer.remaining() < 1) {
                throw new IllegalArgumentException("STREAM metadata key length is missing");
            }
            int keyLength = Byte.toUnsignedInt(buffer.get());
            if (keyLength == 0 || buffer.remaining() < keyLength) {
                throw new IllegalArgumentException("STREAM metadata key is invalid");
            }
            byte[] keyBytes = new byte[keyLength];
            buffer.get(keyBytes);
            if (buffer.remaining() < 2) {
                throw new IllegalArgumentException("STREAM metadata value length is missing");
            }
            int valueLength = Short.toUnsignedInt(buffer.getShort());
            if (buffer.remaining() < valueLength) {
                throw new IllegalArgumentException("STREAM metadata value is invalid");
            }
            byte[] valueBytes = new byte[valueLength];
            buffer.get(valueBytes);
            String key = new String(keyBytes, StandardCharsets.UTF_8);
            String previous = metadata.putIfAbsent(
                key,
                new String(valueBytes, StandardCharsets.UTF_8));
            if (previous != null) {
                throw new IllegalArgumentException("STREAM metadata contains duplicate key");
            }
        }
        if (buffer.hasRemaining()) {
            throw new IllegalArgumentException("STREAM metadata contains trailing bytes");
        }
        return Map.copyOf(metadata);
    }
}
