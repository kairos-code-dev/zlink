package systems.zlink.framework.runtime.spots;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;

final class ZLinkSpotAcceptedJournal {
    private static final int MAGIC = 0x5A4A5231;

    private ZLinkSpotAcceptedJournal() {
    }

    static byte[] encode(ZLinkBackendReceived received) {
        try {
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(buffer);
            output.writeInt(MAGIC);
            output.writeInt(received.result().ordinal());
            writeRoutingId(output, received.routingId());
            writeRoutingId(output, received.spotRid());
            output.writeBoolean(received.requestSeq().isPresent());
            if (received.requestSeq().isPresent()) {
                output.writeLong(received.requestSeq().orElseThrow());
            }
            writeBytes(output, received.applicationMetadata());
            output.writeInt(received.parts().size());
            for (Message part : received.parts()) {
                writeBytes(output, part.toByteArray());
            }
            output.flush();
            return buffer.toByteArray();
        } catch (IOException error) {
            throw new IllegalStateException(
                "failed to encode accepted Spot journal record",
                error);
        }
    }

    static Record decode(byte[] encoded) {
        try {
            DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(encoded));
            if (input.readInt() != MAGIC) {
                throw new IllegalArgumentException(
                    "invalid accepted Spot journal record");
            }
            int result = input.readInt();
            ZLinkBackendRequestResult[] results = ZLinkBackendRequestResult.values();
            if (result < 0 || result >= results.length) {
                throw new IllegalArgumentException(
                    "invalid accepted Spot journal result");
            }
            Optional<RoutingId> routingId = readRoutingId(input);
            Optional<RoutingId> spotRid = readRoutingId(input);
            Optional<Long> requestSequence = input.readBoolean()
                ? Optional.of(input.readLong())
                : Optional.empty();
            byte[] metadata = readBytes(input);
            int count = input.readInt();
            if (count < 0 || count > 65_536) {
                throw new IllegalArgumentException(
                    "invalid accepted Spot journal part count");
            }
            List<byte[]> parts = new ArrayList<>(count);
            for (int index = 0; index < count; index++) {
                parts.add(readBytes(input));
            }
            if (input.available() != 0) {
                throw new IllegalArgumentException(
                    "accepted Spot journal record has trailing bytes");
            }
            return new Record(
                results[result],
                routingId,
                spotRid,
                requestSequence,
                metadata,
                parts);
        } catch (IOException error) {
            throw new IllegalArgumentException(
                "invalid accepted Spot journal record",
                error);
        }
    }

    private static void writeRoutingId(
        DataOutputStream output,
        Optional<RoutingId> value) throws IOException {
        output.writeBoolean(value.isPresent());
        if (value.isPresent()) {
            writeBytes(output, value.orElseThrow().toBytes());
        }
    }

    private static Optional<RoutingId> readRoutingId(
        DataInputStream input) throws IOException {
        return input.readBoolean()
            ? Optional.of(RoutingId.from(readBytes(input)))
            : Optional.empty();
    }

    private static void writeBytes(
        DataOutputStream output,
        byte[] value) throws IOException {
        output.writeInt(value.length);
        output.write(value);
    }

    private static byte[] readBytes(DataInputStream input) throws IOException {
        int length = input.readInt();
        if (length < 0 || length > 64 * 1024 * 1024) {
            throw new IllegalArgumentException(
                "invalid accepted Spot journal byte length");
        }
        byte[] value = input.readNBytes(length);
        if (value.length != length) {
            throw new IllegalArgumentException(
                "truncated accepted Spot journal record");
        }
        return value;
    }

    record Record(
        ZLinkBackendRequestResult result,
        Optional<RoutingId> routingId,
        Optional<RoutingId> spotRid,
        Optional<Long> requestSequence,
        byte[] applicationMetadata,
        List<byte[]> parts) {
        Record {
            applicationMetadata = applicationMetadata.clone();
            parts = parts.stream().map(byte[]::clone).toList();
        }

        @Override
        public byte[] applicationMetadata() {
            return applicationMetadata.clone();
        }

        @Override
        public List<byte[]> parts() {
            return parts.stream().map(byte[]::clone).toList();
        }
    }
}
