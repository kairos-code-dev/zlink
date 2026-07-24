package systems.zlink.framework.configuration;

import java.util.Objects;

public record ZLinkUserSpotFactoryOptions(
    int stableTypeLimit,
    ZLinkUserSpotExecutionMode executionMode) {

    public ZLinkUserSpotFactoryOptions(int stableTypeLimit) {
        this(stableTypeLimit, ZLinkUserSpotExecutionMode.SPOT_WIDE);
    }

    public ZLinkUserSpotFactoryOptions {
        if (stableTypeLimit < 0) {
            throw new IllegalArgumentException(
                "stableTypeLimit must not be negative");
        }
        Objects.requireNonNull(executionMode, "executionMode");
    }
}
