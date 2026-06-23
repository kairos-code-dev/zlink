package systems.zlink.framework.runtime.messaging;

import java.util.Objects;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;

public final class ZLinkPayloadEncoding {
    private ZLinkPayloadEncoding() {
    }

    public static EncodedPayload encode(
        ZLinkMessageSerializer serializer,
        Object payload) {
        Objects.requireNonNull(serializer, "serializer");
        return new EncodedPayload(
            serializer.serialize(payload),
            ZLinkPacketNames.resolve(payload));
    }

    public static String resolvePacketName(Object payload) {
        return ZLinkPacketNames.resolve(payload);
    }

    public record EncodedPayload(
        Message payload,
        String packetName) {
        public EncodedPayload {
            Objects.requireNonNull(payload, "payload");
            Objects.requireNonNull(packetName, "packetName");
        }
    }
}
