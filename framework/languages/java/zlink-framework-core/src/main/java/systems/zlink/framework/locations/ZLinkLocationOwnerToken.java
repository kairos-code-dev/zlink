package systems.zlink.framework.locations;

public record ZLinkLocationOwnerToken(
    String ownerId,
    long leaseGeneration) {
}
