package systems.zlink.codec.messagepack;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Objects;
import org.msgpack.jackson.dataformat.MessagePackFactory;

public final class MessagePackCodec {
    private static final ObjectMapper DEFAULT_OBJECT_MAPPER =
        new ObjectMapper(new MessagePackFactory());

    private MessagePackCodec() {}

    public static systems.zlink.Message toMessage(Object value) {
        Objects.requireNonNull(value, "value");
        try {
            byte[] payload = DEFAULT_OBJECT_MAPPER.writeValueAsBytes(value);
            return systems.zlink.Message.copyOf(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode MessagePack payload", ex);
        }
    }

    public static <T> T parseMessagePack(systems.zlink.Message message,
                                         Class<T> type) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(type, "type");
        try {
            return DEFAULT_OBJECT_MAPPER.readValue(message.toByteArray(), type);
        } catch (java.io.IOException ex) {
            throw new IllegalArgumentException(
                "failed to decode MessagePack payload", ex);
        }
    }
}
