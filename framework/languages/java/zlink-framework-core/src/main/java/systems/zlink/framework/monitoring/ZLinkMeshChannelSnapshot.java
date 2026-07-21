package systems.zlink.framework.monitoring;

public record ZLinkMeshChannelSnapshot(
    String channelName,
    int localWeight,
    long readyMemberCount,
    boolean selectable) {
}
