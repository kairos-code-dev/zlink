package systems.zlink.framework.registry;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkEmbeddedRegistryOptions {
    private String pubEndpoint;
    private String routerEndpoint;
    private final List<String> peerPubEndpoints = new ArrayList<>();

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

    public void addPeer(String pubEndpoint) {
        peerPubEndpoints.add(requireEndpoint(pubEndpoint, "peer pubEndpoint"));
    }

    public List<String> peerPubEndpoints() {
        return List.copyOf(peerPubEndpoints);
    }

    public void validate() {
        requireEndpoint(pubEndpoint, "pubEndpoint");
        requireEndpoint(routerEndpoint, "routerEndpoint");
    }

    private static String requireEndpoint(String endpoint, String label) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException(label + " is required");
        }
        return endpoint;
    }
}
