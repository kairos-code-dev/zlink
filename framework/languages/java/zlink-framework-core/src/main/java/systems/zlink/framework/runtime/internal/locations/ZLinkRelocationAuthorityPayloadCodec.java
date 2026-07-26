package systems.zlink.framework.runtime.internal.locations;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.Objects;
import java.util.UUID;

/** Encodes the cross-language ZLAR authority publication payload. */
final class ZLinkRelocationAuthorityPayloadCodec {
    private static final int MAGIC = 0x5a4c4152;
    private static final short VERSION = 1;
    private static final int MAX_BYTES = 1024 * 1024;

    private ZLinkRelocationAuthorityPayloadCodec() {
    }

    static byte[] encode(Payload payload) {
        Objects.requireNonNull(payload, "payload");
        Writer writer = new Writer();
        writer.i32(MAGIC);
        writer.i16(VERSION);
        writer.text16(payload.reference());
        writer.i32((int) payload.checksumCrc32c());
        writer.uuidDotNet(payload.aggregateId());
        writer.i64(payload.aggregateGeneration());
        writer.bytes32(payload.inventoryDigest());
        writer.text16(payload.targetOwnerId());
        writer.i64(payload.targetOwnerLeaseGeneration());
        writer.bytes32(payload.applicationPayload());
        byte[] encoded = writer.toByteArray();
        if (encoded.length > MAX_BYTES) {
            throw new IllegalArgumentException(
                "authority relocation payload exceeds 1 MiB");
        }
        return encoded;
    }

    static Payload decode(byte[] encoded) {
        try {
            Reader reader = new Reader(encoded);
            if (reader.i32() != MAGIC || reader.i16() != VERSION) {
                return null;
            }
            String reference = reader.text16();
            long checksum = Integer.toUnsignedLong(reader.i32());
            UUID aggregateId = reader.uuidDotNet();
            long generation = reader.i64();
            byte[] digest = reader.bytes32();
            String ownerId = reader.text16();
            long leaseGeneration = reader.i64();
            byte[] applicationPayload = reader.bytes32();
            if (aggregateId.equals(new UUID(0, 0)) || generation <= 0
                || digest.length != 32 || leaseGeneration <= 0
                || !reader.atEnd()) {
                return null;
            }
            return new Payload(
                reference,
                checksum,
                aggregateId,
                generation,
                digest,
                ownerId,
                leaseGeneration,
                applicationPayload);
        } catch (RuntimeException failure) {
            return null;
        }
    }

    record Payload(
        String reference,
        long checksumCrc32c,
        UUID aggregateId,
        long aggregateGeneration,
        byte[] inventoryDigest,
        String targetOwnerId,
        long targetOwnerLeaseGeneration,
        byte[] applicationPayload) {
        Payload {
            Objects.requireNonNull(reference, "reference");
            Objects.requireNonNull(aggregateId, "aggregateId");
            inventoryDigest = Objects.requireNonNull(
                inventoryDigest,
                "inventoryDigest").clone();
            Objects.requireNonNull(targetOwnerId, "targetOwnerId");
            applicationPayload = Objects.requireNonNull(
                applicationPayload,
                "applicationPayload").clone();
        }

        @Override
        public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }

        @Override
        public byte[] applicationPayload() {
            return applicationPayload.clone();
        }
    }

    private static final class Writer {
        private final ByteArrayOutputStream output = new ByteArrayOutputStream();

        void i16(short value) {
            output.writeBytes(ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN)
                .putShort(value).array());
        }

        void i32(int value) {
            output.writeBytes(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(value).array());
        }

        void i64(long value) {
            output.writeBytes(ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
                .putLong(value).array());
        }

        void text16(String value) {
            byte[] bytes = Objects.requireNonNull(value, "value")
                .getBytes(StandardCharsets.UTF_8);
            if (bytes.length < 1 || bytes.length > 0xffff) {
                throw new IllegalArgumentException("text length is outside 1..65535");
            }
            i16((short) bytes.length);
            output.writeBytes(bytes);
        }

        void bytes32(byte[] value) {
            Objects.requireNonNull(value, "value");
            i32(value.length);
            output.writeBytes(value);
        }

        void uuidDotNet(UUID value) {
            Objects.requireNonNull(value, "value");
            ByteBuffer canonical = ByteBuffer.allocate(16)
                .putLong(value.getMostSignificantBits())
                .putLong(value.getLeastSignificantBits());
            byte[] bytes = canonical.array();
            reverse(bytes, 0, 4);
            reverse(bytes, 4, 2);
            reverse(bytes, 6, 2);
            output.writeBytes(bytes);
        }

        byte[] toByteArray() {
            return output.toByteArray();
        }
    }

    private static final class Reader {
        private final ByteBuffer input;

        Reader(byte[] encoded) {
            input = ByteBuffer.wrap(Objects.requireNonNull(encoded, "encoded"))
                .order(ByteOrder.LITTLE_ENDIAN);
        }

        short i16() {
            return input.getShort();
        }

        int i32() {
            return input.getInt();
        }

        long i64() {
            return input.getLong();
        }

        String text16() {
            byte[] bytes = take(Short.toUnsignedInt(i16()));
            if (bytes.length == 0) {
                throw new IllegalArgumentException("empty text");
            }
            try {
                return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes)).toString();
            } catch (CharacterCodingException failure) {
                throw new IllegalArgumentException("invalid UTF-8", failure);
            }
        }

        byte[] bytes32() {
            int size = i32();
            if (size < 0 || size > MAX_BYTES) {
                throw new IllegalArgumentException("invalid byte length");
            }
            return take(size);
        }

        UUID uuidDotNet() {
            byte[] bytes = take(16);
            reverse(bytes, 0, 4);
            reverse(bytes, 4, 2);
            reverse(bytes, 6, 2);
            ByteBuffer canonical = ByteBuffer.wrap(bytes);
            return new UUID(canonical.getLong(), canonical.getLong());
        }

        boolean atEnd() {
            return !input.hasRemaining();
        }

        private byte[] take(int size) {
            if (size > input.remaining()) {
                throw new IllegalArgumentException("truncated payload");
            }
            byte[] result = new byte[size];
            input.get(result);
            return result;
        }
    }

    private static void reverse(byte[] bytes, int offset, int length) {
        for (int left = offset, right = offset + length - 1;
             left < right;
             left++, right--) {
            byte value = bytes[left];
            bytes[left] = bytes[right];
            bytes[right] = value;
        }
    }
}
