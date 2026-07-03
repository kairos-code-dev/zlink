package systems.zlink.framework.locations;

public record ZLinkLocationServiceSummaryFilter(
    String meshName,
    ZLinkLocationAutoConnectType autoConnectType,
    ZLinkLocationRole role) {

    public static ZLinkLocationServiceSummaryFilter all() {
        return new ZLinkLocationServiceSummaryFilter(null, null, null);
    }
}
