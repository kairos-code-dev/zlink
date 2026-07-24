package systems.zlink.framework.configuration;

public record ZLinkInstanceSpotFactoryOptions(int stableTypeLimit) {
    public ZLinkInstanceSpotFactoryOptions {
        if (stableTypeLimit < 0) {
            throw new IllegalArgumentException(
                "stableTypeLimit must not be negative");
        }
    }
}
