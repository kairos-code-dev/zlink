package dev.kairoscode.zlink.codec.json;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class JsonCodec<T> {
    private final ObjectMapper objectMapper;

    public JsonCodec() {
        this(new ObjectMapper());
    }

    public JsonCodec(ObjectMapper objectMapper) {
        this.objectMapper = Objects.requireNonNull(objectMapper, "objectMapper");
    }

    public dev.kairoscode.zlink.Message toMessage(T value) {
        Objects.requireNonNull(value, "value");
        try {
            byte[] payload = objectMapper.writeValueAsBytes(value);
            return dev.kairoscode.zlink.Message.copyOf(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode JSON payload", ex);
        }
    }

    public T fromMessage(dev.kairoscode.zlink.Message message, Class<T> type) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(type, "type");
        try {
            return objectMapper.readValue(message.toByteArray(), type);
        } catch (java.io.IOException ex) {
            String preview = new String(message.toByteArray(), StandardCharsets.UTF_8);
            throw new IllegalArgumentException(
                "failed to decode JSON payload: " + preview, ex);
        }
    }
}
