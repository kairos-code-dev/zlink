package systems.zlink.framework.runtime.messaging;

import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class ZLinkPacketNames {
    private ZLinkPacketNames() {
    }

    public static String resolve(Object payload) {
        if (payload == null) {
            return "Null";
        }
        if (payload instanceof systems.zlink.contracts.messaging.Message) {
            throw new ZLinkConfigurationException(
                "binding Message is not a framework business payload type; use a DTO, ZLinkMessage, or ZLinkEncodedPayload");
        }
        return resolve(payload.getClass());
    }

    public static String resolve(Class<?> payloadType) {
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

    public static String resolve(Class<?> payloadType, String explicitPacketName) {
        return explicitPacketName == null || explicitPacketName.isBlank()
            ? resolve(payloadType)
            : explicitPacketName;
    }
}
