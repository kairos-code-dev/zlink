package systems.zlink.codec.json;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class JsonCodec {
    private static final ObjectMapper DEFAULT_OBJECT_MAPPER = new ObjectMapper();

    private JsonCodec() {}

    public static systems.zlink.contracts.Message toMessage(Object value) {
        Objects.requireNonNull(value, "value");
        try {
            byte[] payload = DEFAULT_OBJECT_MAPPER.writeValueAsBytes(value);
            return systems.zlink.contracts.Message.copyOf(payload);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException(
                "failed to encode JSON payload", ex);
        }
    }

    public static <T> T parseJson(systems.zlink.contracts.Message message,
                                  Class<T> type) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(type, "type");
        try {
            return DEFAULT_OBJECT_MAPPER.readValue(message.toByteArray(), type);
        } catch (java.io.IOException ex) {
            String preview = new String(message.toByteArray(), StandardCharsets.UTF_8);
            throw new IllegalArgumentException(
                "failed to decode JSON payload: " + preview, ex);
        }
    }
}
