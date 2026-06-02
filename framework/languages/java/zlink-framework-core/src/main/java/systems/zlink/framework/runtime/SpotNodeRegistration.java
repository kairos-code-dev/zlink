package systems.zlink.framework.runtime;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

final class SpotNodeRegistration {
    private final String meshName;
    private final String nodeName;
    private final List<Class<? extends ZLinkSpot>> spotFactories = new ArrayList<>();
    private final List<Class<? extends ZLinkEntrySpot>> entrySpots = new ArrayList<>();
    private String routerBind;
    private String pubBind;

    SpotNodeRegistration(String meshName, String nodeName) {
        this.meshName = meshName;
        this.nodeName = nodeName;
    }

    String meshName() {
        return meshName;
    }

    String nodeName() {
        return nodeName;
    }

    List<Class<? extends ZLinkSpot>> spotFactories() {
        return List.copyOf(spotFactories);
    }

    String routerBind() {
        return routerBind;
    }

    String pubBind() {
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

    void validate() {
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
