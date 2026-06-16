package systems.zlink.framework.runtime.messaging;

import java.util.Objects;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class ZLinkPayloadEncoding {
    private ZLinkPayloadEncoding() {
    }

    public static EncodedPayload encode(
        ZLinkMessageSerializer serializer,
        Object payload) {
        Objects.requireNonNull(serializer, "serializer");
        return new EncodedPayload(
            serializer.serialize(payload),
            resolvePacketName(payload));
    }

    public static String resolvePacketName(Object payload) {
        if (payload == null) {
            return "Null";
        }
        if (payload instanceof Message) {
            return "Message";
        }
        Class<?> payloadType = payload.getClass();
        ZLinkPacket packet = payloadType.getAnnotation(ZLinkPacket.class);
        if (packet != null && !packet.value().isBlank()) {
            return packet.value();
        }
        String simpleName = payloadType.getSimpleName();
        if (!simpleName.isBlank()) {
            return simpleName;
        }
        String typeName = payloadType.getName();
        if (!typeName.isBlank()) {
            return typeName;
        }
        throw new ZLinkConfigurationException(
            "packet name cannot be resolved for payload type");
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
