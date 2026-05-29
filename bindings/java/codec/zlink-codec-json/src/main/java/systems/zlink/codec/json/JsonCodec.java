package systems.zlink.codec.json;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class JsonCodec {
    private static final ObjectMapper DEFAULT_OBJECT_MAPPER = new ObjectMapper();

    private JsonCodec() {}

    public static systems.zlink.contracts.messaging.Message toMessage(Object value) {
        Objects.requireNonNull(value, "value");
        try {
            byte[] payload = DEFAULT_OBJECT_MAPPER.writeValueAsBytes(value);
            return systems.zlink.contracts.messaging.Message.from(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode JSON payload", ex);
        }
    }

    public static <T> T parseJson(systems.zlink.contracts.messaging.Message message,
                                  Class<T> type) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(type, "type");
        try {
            return DEFAULT_OBJECT_MAPPER.readValue(
                new ByteBufferInputStream(message.dataBuffer()), type);
        } catch (java.io.IOException ex) {
            String preview = new String(message.toByteArray(), StandardCharsets.UTF_8);
            throw new IllegalArgumentException(
                "failed to decode JSON payload: " + preview, ex);
        }
    }

    private static final class ByteBufferInputStream extends InputStream {
        private final ByteBuffer buffer;

        private ByteBufferInputStream(ByteBuffer buffer) {
            this.buffer = Objects.requireNonNull(buffer, "buffer").slice();
        }

        @Override
        public int read() {
            if (!buffer.hasRemaining()) {
                return -1;
            }
            return buffer.get() & 0xff;
        }

        @Override
        public int read(byte[] bytes, int offset, int length) {
            Objects.checkFromIndexSize(offset, length, bytes.length);
            if (length == 0) {
                return 0;
            }
            if (!buffer.hasRemaining()) {
                return -1;
            }
            int count = Math.min(length, buffer.remaining());
            buffer.get(bytes, offset, count);
            return count;
        }

        @Override
        public long skip(long n) {
            if (n <= 0 || !buffer.hasRemaining()) {
                return 0;
            }
            int count = (int) Math.min(n, buffer.remaining());
            buffer.position(buffer.position() + count);
            return count;
        }

        @Override
        public int available() {
            return buffer.remaining();
        }
    }
}
