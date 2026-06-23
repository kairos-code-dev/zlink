package systems.zlink.framework.runtime.messaging;

import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.framework.ZLinkMessageSerializer;

public final class ZLinkStringMessageSerializer implements ZLinkMessageSerializer {
    @Override
    public <T> ZLinkEncodedPayload serialize(T value) {
        rejectRawPayloadType(value == null ? null : value.getClass());
        return ZLinkEncodedPayload.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
    }

    @Override
    public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
        rejectRawPayloadType(type);
        if (type == String.class) {
            return type.cast(new String(payload.bytes(), StandardCharsets.UTF_8));
        }
        throw new IllegalArgumentException("unsupported message type: " + type.getName());
    }

    private static void rejectRawPayloadType(Class<?> type) {
        if (type == Message.class || type == byte[].class) {
            throw new IllegalArgumentException(
                "binding Message and byte[] are not framework business payload types; use a DTO, ZLinkMessage, or ZLinkEncodedPayload");
        }
    }
}
