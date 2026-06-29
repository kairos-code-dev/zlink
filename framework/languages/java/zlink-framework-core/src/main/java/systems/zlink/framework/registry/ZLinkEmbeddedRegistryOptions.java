package systems.zlink.framework.registry;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkEmbeddedRegistryOptions {
    public static final Duration DEFAULT_HEARTBEAT_INTERVAL = Duration.ofSeconds(5);
    public static final Duration DEFAULT_HEARTBEAT_TIMEOUT = Duration.ofSeconds(15);
    public static final Duration DEFAULT_BROADCAST_INTERVAL = Duration.ofSeconds(30);

    private int registryId;
    private String pubEndpoint;
    private String routerEndpoint;
    private Duration heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;
    private Duration heartbeatTimeout = DEFAULT_HEARTBEAT_TIMEOUT;
    private Duration broadcastInterval = DEFAULT_BROADCAST_INTERVAL;
    private final List<String> peerPubEndpoints = new ArrayList<>();

    public int registryId() {
        return registryId;
    }

    public void setRegistryId(int registryId) {
        if (registryId < 0) {
            throw new ZLinkConfigurationException("registryId must be non-negative");
        }
        this.registryId = registryId;
    }

    public String pubEndpoint() {
        return pubEndpoint;
    }

    public void setPubEndpoint(String pubEndpoint) {
        this.pubEndpoint = requireEndpoint(pubEndpoint, "pubEndpoint");
    }

    public String routerEndpoint() {
        return routerEndpoint;
    }

    public void setRouterEndpoint(String routerEndpoint) {
        this.routerEndpoint = requireEndpoint(routerEndpoint, "routerEndpoint");
    }

    public Duration heartbeatInterval() {
        return heartbeatInterval;
    }

    public void setHeartbeatInterval(Duration heartbeatInterval) {
        this.heartbeatInterval = requirePositiveDuration(
            heartbeatInterval,
            "heartbeatInterval");
    }

    public Duration heartbeatTimeout() {
        return heartbeatTimeout;
    }

    public void setHeartbeatTimeout(Duration heartbeatTimeout) {
        this.heartbeatTimeout = requirePositiveDuration(
            heartbeatTimeout,
            "heartbeatTimeout");
    }

    public Duration broadcastInterval() {
        return broadcastInterval;
    }

    public void setBroadcastInterval(Duration broadcastInterval) {
        this.broadcastInterval = requirePositiveDuration(
            broadcastInterval,
            "broadcastInterval");
    }

    public void addPeer(String pubEndpoint) {
        peerPubEndpoints.add(requireEndpoint(pubEndpoint, "peer pubEndpoint"));
    }

    public List<String> peerPubEndpoints() {
        return List.copyOf(peerPubEndpoints);
    }

    public void validate() {
        requireEndpoint(pubEndpoint, "pubEndpoint");
        requireEndpoint(routerEndpoint, "routerEndpoint");
        requirePositiveDuration(heartbeatInterval, "heartbeatInterval");
        requirePositiveDuration(heartbeatTimeout, "heartbeatTimeout");
        requirePositiveDuration(broadcastInterval, "broadcastInterval");
        requireHeartbeatTimeoutAfterInterval();
    }

    private static String requireEndpoint(String endpoint, String label) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException(label + " is required");
        }
        return endpoint;
    }

    private static Duration requirePositiveDuration(Duration duration, String label) {
        if (duration == null || duration.isZero() || duration.isNegative()) {
            throw new ZLinkConfigurationException(label + " must be greater than zero");
        }
        return duration;
    }

    private void requireHeartbeatTimeoutAfterInterval() {
        if (heartbeatTimeout.compareTo(heartbeatInterval) <= 0) {
            throw new ZLinkConfigurationException(
                "heartbeatTimeout must be greater than heartbeatInterval");
        }
    }
}
