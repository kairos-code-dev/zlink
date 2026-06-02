package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class SpotChannelClientRegistration {
    private final String channelName;
    private final List<String> manualConnections = new ArrayList<>();

    SpotChannelClientRegistration(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException(
                "attached client/server channel name is required");
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
                "manual attached channel endpoint is required");
        }
        manualConnections.add(endpoint);
    }
}
