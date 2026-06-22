package systems.zlink.framework.configuration;

public record ZLinkMessageFlowEvent(
    ZLinkMessageFlowPhase phase,
    ZLinkDispatchErrorSurface surface,
    ZLinkDispatchMessageKind messageKind,
    String packetName,
    String channelName,
    String topic,
    String correlationId,
    String sourceRid,
    String spotRid,
    String actorId,
    Long messageSize) {
}
