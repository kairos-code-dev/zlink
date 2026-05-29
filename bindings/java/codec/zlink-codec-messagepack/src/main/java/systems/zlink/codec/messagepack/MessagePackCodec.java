package systems.zlink.codec.messagepack;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.Objects;
import org.msgpack.jackson.dataformat.MessagePackFactory;

public final class MessagePackCodec {
    private static final ObjectMapper DEFAULT_OBJECT_MAPPER =
        new ObjectMapper(new MessagePackFactory());

    private MessagePackCodec() {}

    public static systems.zlink.contracts.messaging.Message toMessage(Object value) {
        Objects.requireNonNull(value, "value");
        try {
            byte[] payload = DEFAULT_OBJECT_MAPPER.writeValueAsBytes(value);
            return systems.zlink.contracts.messaging.Message.from(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode MessagePack payload", ex);
        }
    }

    public static <T> T parseMessagePack(systems.zlink.contracts.messaging.Message message,
                                         Class<T> type) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(type, "type");
        try {
            return DEFAULT_OBJECT_MAPPER.readValue(
                new ByteBufferInputStream(message.dataBuffer()), type);
        } catch (java.io.IOException ex) {
            throw new IllegalArgumentException(
                "failed to decode MessagePack payload", ex);
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
