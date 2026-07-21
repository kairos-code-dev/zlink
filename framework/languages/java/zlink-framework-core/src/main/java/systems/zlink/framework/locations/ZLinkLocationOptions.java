package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

public final class ZLinkLocationOptions {
    private Duration heartbeatInterval = Duration.ofSeconds(10);
    private Duration ownerLeaseTtl = Duration.ofSeconds(30);
    private Duration pollingInterval = Duration.ofSeconds(1);
    private int listPageSize = 1000;
    private Duration storeFailureGrace = Duration.ofSeconds(30);
    private Duration routingIdFencingMargin = Duration.ofSeconds(5);
    private Duration ownerLeaseRenewTimeout = Duration.ofSeconds(3);
    private final Map<String, String> spotRouterChannels = new LinkedHashMap<>();

    public Duration heartbeatInterval() {
        return heartbeatInterval;
    }

    public void setHeartbeatInterval(Duration heartbeatInterval) {
        this.heartbeatInterval = requirePositive(heartbeatInterval, "heartbeatInterval");
    }

    public Duration ownerLeaseTtl() {
        return ownerLeaseTtl;
    }

    public void setOwnerLeaseTtl(Duration ownerLeaseTtl) {
        this.ownerLeaseTtl = requirePositive(ownerLeaseTtl, "ownerLeaseTtl");
    }

    public Duration pollingInterval() {
        return pollingInterval;
    }

    public void setPollingInterval(Duration pollingInterval) {
        this.pollingInterval = requirePositive(pollingInterval, "pollingInterval");
    }

    public int listPageSize() {
        return listPageSize;
    }

    public void setListPageSize(int listPageSize) {
        if (listPageSize <= 0) {
            throw new IllegalArgumentException("listPageSize must be positive.");
        }
        this.listPageSize = listPageSize;
    }

    public Duration storeFailureGrace() {
        return storeFailureGrace;
    }

    public void setStoreFailureGrace(Duration storeFailureGrace) {
        this.storeFailureGrace = requirePositive(storeFailureGrace, "storeFailureGrace");
    }

    public Duration routingIdFencingMargin() {
        return routingIdFencingMargin;
    }

    public void setRoutingIdFencingMargin(Duration routingIdFencingMargin) {
        this.routingIdFencingMargin = requirePositive(
            routingIdFencingMargin,
            "routingIdFencingMargin");
    }

    public Duration ownerLeaseRenewTimeout() {
        return ownerLeaseRenewTimeout;
    }

    public void setOwnerLeaseRenewTimeout(Duration ownerLeaseRenewTimeout) {
        this.ownerLeaseRenewTimeout = requirePositive(
            ownerLeaseRenewTimeout,
            "ownerLeaseRenewTimeout");
    }

    public Map<String, String> spotRouterChannels() {
        return Map.copyOf(spotRouterChannels);
    }

    public void setSpotRouterChannel(String spotMeshName, String routerChannelId) {
        spotRouterChannels.put(
            requireText(spotMeshName, "spotMeshName"),
            requireText(routerChannelId, "routerChannelId"));
    }

    private static Duration requirePositive(Duration value, String name) {
        if (value == null || value.isZero() || value.isNegative()) {
            throw new IllegalArgumentException(name + " must be positive.");
        }
        return value;
    }

    private static String requireText(String value, String name) {
        Objects.requireNonNull(value, name);
        if (value.isBlank()) {
            throw new IllegalArgumentException(name + " is required.");
        }
        return value;
    }
}
