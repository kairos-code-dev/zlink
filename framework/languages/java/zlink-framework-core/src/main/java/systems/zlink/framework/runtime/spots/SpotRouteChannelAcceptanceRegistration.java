package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class SpotRouteChannelAcceptanceRegistration {
    private final String channelName;
    private final List<String> manualConnections = new ArrayList<>();

    SpotRouteChannelAcceptanceRegistration(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException(
                "accepted SPOT route channel name is required");
        }
        this.channelName = channelName;
    }

    public String channelName() {
        return channelName;
    }

    public List<String> manualConnections() {
        return List.copyOf(manualConnections);
    }

    void addManualConnection(String endpoint) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException(
                "manual SPOT route channel endpoint is required");
        }
        manualConnections.add(endpoint);
    }
}
