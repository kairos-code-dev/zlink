package systems.zlink.framework.locations;

public record ZLinkLocationServiceSummaryFilter(
    String meshName) {

    public static ZLinkLocationServiceSummaryFilter all() {
        return new ZLinkLocationServiceSummaryFilter(null);
    }
}
