package systems.zlink.framework.runtime.streams;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Optional;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class ZLinkStreamHeaderCodec {
    static final int KIND_SEND = 1;
    static final int KIND_REQUEST = 2;
    static final int KIND_RESPONSE = 3;
    private static final int FLAG_HAS_REQUEST_SEQ = 0x01;

    private ZLinkStreamHeaderCodec() {
    }

    static ZLinkStreamHeader decodeOrPlain(byte[] bytes) {
        if (bytes.length < 4 || !isKnownKind(bytes[0])) {
            return new ZLinkStreamHeader(
                new String(bytes, StandardCharsets.UTF_8),
                Map.of(),
                Optional.empty());
        }
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
        return new ZLinkStreamHeader(packetName, Map.of(), requestSeq);
    }

    static byte[] encode(int kind, String packetName, Optional<Long> requestSeq) {
        if (packetName == null || packetName.isBlank()) {
            throw new IllegalArgumentException("packetName is required");
        }
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buffer = ByteBuffer.allocate(
            3 + (requestSeq.isPresent() ? Long.BYTES : 0) + 1 + name.length);
        buffer.put((byte) kind);
        buffer.put((byte) 0);
        buffer.put((byte) (requestSeq.isPresent() ? FLAG_HAS_REQUEST_SEQ : 0));
        requestSeq.ifPresent(value -> {
            if (value == 0) {
                throw new IllegalArgumentException("STREAM request sequence must not be zero");
            }
            buffer.putLong(value);
        });
        buffer.put((byte) name.length);
        buffer.put(name);
        return buffer.array();
    }

    private static boolean isKnownKind(byte value) {
        int kind = Byte.toUnsignedInt(value);
        return kind >= KIND_SEND && kind <= 5;
    }
}
