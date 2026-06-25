package systems.zlink.framework.configuration;

public record ZLinkMessageFlowEvent(
    ZLinkMessageFlowOutcome outcome,
    ZLinkDispatchErrorSurface surface,
    ZLinkDispatchMessageKind messageKind,
    String packetName,
    String channelName,
    String topic,
    String correlationId,
    String sourceRid,
    String spotRid,
    String actorId,
    Long messageSize,
    ZLinkDispatchErrorReason errorReason,
    ZLinkDispatchErrorAction errorAction,
    String errorType,
    String errorMessage) {
    public ZLinkMessageFlowEvent(
        ZLinkMessageFlowOutcome outcome,
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
        this(outcome, surface, messageKind, packetName, channelName, topic,
            correlationId, sourceRid, spotRid, actorId, messageSize,
            null, null, null, null);
    }
}
