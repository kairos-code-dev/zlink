package systems.zlink.framework.messaging;

import java.util.Objects;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;

public final class ZLinkMessage {
    private static final byte[] EMPTY_PAYLOAD = new byte[0];

    private final Object value;
    private final byte[] payload;
    private final ZLinkMessageSerializer serializer;

    private ZLinkMessage(Object value, byte[] payload, ZLinkMessageSerializer serializer) {
        this.value = value;
        this.payload = payload;
        this.serializer = serializer;
    }

    public static ZLinkMessage empty() {
        return new ZLinkMessage(null, EMPTY_PAYLOAD, null);
    }

    public static ZLinkMessage of(Object value) {
        if (value instanceof ZLinkMessage message) {
            return message;
        }
        return new ZLinkMessage(Objects.requireNonNull(value, "value"), null, null);
    }

    public static ZLinkMessage fromMessage(Message message, ZLinkMessageSerializer serializer) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(serializer, "serializer");
        return new ZLinkMessage(null, message.toByteArray(), serializer);
    }

    public boolean isEmpty() {
        return value == null && payload != null && payload.length == 0;
    }

    public <T> T decode(Class<T> type) {
        Objects.requireNonNull(type, "type");
        if (value != null) {
            if (type.isInstance(value)) {
                return type.cast(value);
            }
            Message encoded = serializerForEncode().serialize(value);
            try {
                return serializerForEncode().deserialize(encoded, type);
            } finally {
                encoded.close();
            }
        }
        Message message = Message.from(payload == null ? EMPTY_PAYLOAD : payload);
        try {
            return serializerForDecode().deserialize(message, type);
        } finally {
            message.close();
        }
    }

    public Message toMessage(ZLinkMessageSerializer serializer) {
        Objects.requireNonNull(serializer, "serializer");
        if (value != null) {
            return serializer.serialize(value);
        }
        return Message.from(payload == null ? EMPTY_PAYLOAD : payload);
    }

    private ZLinkMessageSerializer serializerForDecode() {
        if (serializer == null) {
            throw new IllegalStateException("message does not have a codec registry for decode");
        }
        return serializer;
    }

    private ZLinkMessageSerializer serializerForEncode() {
        if (serializer != null) {
            return serializer;
        }
        throw new IllegalStateException("message does not have a codec registry for re-encode");
    }
}
