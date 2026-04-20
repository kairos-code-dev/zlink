package dev.kairoscode.zlink.codec.messagepack;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Objects;
import org.msgpack.jackson.dataformat.MessagePackFactory;

public final class MessagePackCodec<T> {
    private final ObjectMapper objectMapper;

    public MessagePackCodec() {
        this(new ObjectMapper(new MessagePackFactory()));
    }

    public MessagePackCodec(ObjectMapper objectMapper) {
        this.objectMapper = Objects.requireNonNull(objectMapper, "objectMapper");
    }

    public dev.kairoscode.zlink.Message toMessage(T value) {
        Objects.requireNonNull(value, "value");
        try {
            byte[] payload = objectMapper.writeValueAsBytes(value);
            return dev.kairoscode.zlink.Message.copyOf(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode MessagePack payload", ex);
        }
    }

    public T fromMessage(dev.kairoscode.zlink.Message message, Class<T> type) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(type, "type");
        try {
            return objectMapper.readValue(message.toByteArray(), type);
        } catch (java.io.IOException ex) {
            throw new IllegalArgumentException(
                "failed to decode MessagePack payload", ex);
        }
    }
}
