package systems.zlink.framework.configuration;

public record ZLinkMessageDispatchErrorEvent(
    ZLinkDispatchErrorSurface surface,
    ZLinkDispatchMessageKind messageKind,
    ZLinkDispatchErrorReason reason,
    ZLinkDispatchErrorAction action,
    String packetName,
    String channelName,
    String topic,
    String spotRid,
    String actorId,
    String sourceRid,
    String correlationId,
    Throwable exception) {
}
