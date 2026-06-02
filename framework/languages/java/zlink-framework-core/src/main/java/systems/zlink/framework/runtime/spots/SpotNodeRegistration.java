package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public final class SpotNodeRegistration {
    private final String meshName;
    private final String nodeName;
    private final List<Class<? extends ZLinkSpot>> spotFactories = new ArrayList<>();
    private final List<Class<? extends ZLinkEntrySpot>> entrySpots = new ArrayList<>();
    private String routerBind;
    private String pubBind;

    public SpotNodeRegistration(String meshName, String nodeName) {
        this.meshName = meshName;
        this.nodeName = nodeName;
    }

    public String meshName() {
        return meshName;
    }

    public String nodeName() {
        return nodeName;
    }

    public List<Class<? extends ZLinkSpot>> spotFactories() {
        return List.copyOf(spotFactories);
    }

    public String routerBind() {
        return routerBind;
    }

    public String pubBind() {
        return pubBind;
    }

    void setRouterBind(String endpoint) {
        routerBind = requireEndpoint(endpoint, "router endpoint");
    }

    void setPubBind(String endpoint) {
        pubBind = requireEndpoint(endpoint, "pub endpoint");
    }

    void addSpotFactory(Class<? extends ZLinkSpot> spotType) {
        if (spotType == null) {
            throw new ZLinkConfigurationException("spot factory type is required");
        }
        spotFactories.add(spotType);
    }

    void addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType) {
        if (entrySpotType == null) {
            throw new ZLinkConfigurationException("entry spot type is required");
        }
        entrySpots.add(entrySpotType);
    }

    public void validate() {
        Set<Class<? extends ZLinkSpot>> spotTypes = new HashSet<>();
        for (Class<? extends ZLinkSpot> spotFactory : spotFactories) {
            if (!spotTypes.add(spotFactory)) {
                throw new ZLinkConfigurationException(
                    "duplicate spot factory type on node: " + nodeName);
            }
        }
        if (entrySpots.size() > 1) {
            throw new ZLinkConfigurationException(
                "spot node registers multiple entry spots: " + nodeName);
        }
    }

    private static String requireEndpoint(String endpoint, String label) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException(label + " is required");
        }
        return endpoint;
    }
}
