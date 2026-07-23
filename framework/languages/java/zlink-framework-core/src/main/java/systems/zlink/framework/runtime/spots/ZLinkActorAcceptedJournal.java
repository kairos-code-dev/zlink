package systems.zlink.framework.runtime.spots;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;

final class ZLinkActorAcceptedJournal {
    private static final int MAGIC = 0x5A414A31;

    private ZLinkActorAcceptedJournal() {
    }

    static byte[] encode(
        String actorId,
        ZLinkStreamHeader header,
        Message payload) {
        try {
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(buffer);
            output.writeInt(MAGIC);
            write(output, actorId.getBytes(StandardCharsets.UTF_8));
            write(output, ZLinkStreamHeaderCodec.encode(header));
            write(output, payload.toByteArray());
            output.flush();
            return buffer.toByteArray();
        } catch (IOException error) {
            throw new IllegalStateException(
                "failed to encode accepted Actor journal record",
                error);
        }
    }

    static Record decode(byte[] encoded) {
        try {
            DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(encoded));
            if (input.readInt() != MAGIC) {
                throw new IllegalArgumentException(
                    "invalid accepted Actor journal record");
            }
            String actorId = new String(read(input), StandardCharsets.UTF_8);
            ZLinkStreamHeader header = ZLinkStreamHeaderCodec.decodeOrPlain(
                read(input));
            byte[] payload = read(input);
            if (input.available() != 0) {
                throw new IllegalArgumentException(
                    "accepted Actor journal record has trailing bytes");
            }
            return new Record(actorId, header, payload);
        } catch (IOException error) {
            throw new IllegalArgumentException(
                "invalid accepted Actor journal record",
                error);
        }
    }

    private static void write(
        DataOutputStream output,
        byte[] value) throws IOException {
        output.writeInt(value.length);
        output.write(value);
    }

    private static byte[] read(DataInputStream input) throws IOException {
        int length = input.readInt();
        if (length < 0 || length > 64 * 1024 * 1024) {
            throw new IllegalArgumentException(
                "invalid accepted Actor journal byte length");
        }
        byte[] value = input.readNBytes(length);
        if (value.length != length) {
            throw new IllegalArgumentException(
                "truncated accepted Actor journal record");
        }
        return value;
    }

    record Record(
        String actorId,
        ZLinkStreamHeader header,
        byte[] payload) {
        Record {
            payload = payload.clone();
        }

        @Override
        public byte[] payload() {
            return payload.clone();
        }
    }
}
