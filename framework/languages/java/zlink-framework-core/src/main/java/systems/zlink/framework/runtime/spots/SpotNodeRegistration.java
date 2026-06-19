package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkEntrySpotOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public final class SpotNodeRegistration {
    private final String meshName;
    private final String nodeName;
    private final List<Class<? extends ZLinkSpot<?>>> spotFactories = new ArrayList<>();
    private final List<Class<? extends ZLinkEntrySpot<?>>> entrySpots = new ArrayList<>();
    private final Map<String, SpotPublisherClientRegistration> attachedSpotPublisherClients =
        new LinkedHashMap<>();
    private final Map<String, SpotChannelClientRegistration> attachedChannelClients =
        new LinkedHashMap<>();
    private final Map<String, SpotRouteChannelAcceptanceRegistration> acceptedSpotRouteChannels =
        new LinkedHashMap<>();
    private final List<RouterManualConnection> routerManualConnections = new ArrayList<>();
    private final List<String> pubSubManualConnections = new ArrayList<>();
    private boolean routerEnabled;
    private boolean pubSubEnabled;
    private String routerBind;
    private String pubBind;
    private RoutingId routerRoutingId;
    private RoutingId pubSubRoutingId;
    private RoutingId entrySpotRoutingId;

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

    public List<Class<? extends ZLinkSpot<?>>> spotFactories() {
        return List.copyOf(spotFactories);
    }

    public List<Class<? extends ZLinkEntrySpot<?>>> entrySpots() {
        return List.copyOf(entrySpots);
    }

    public String routerBind() {
        return routerBind;
    }

    public String pubBind() {
        return pubBind;
    }

    public RoutingId routerRoutingId() {
        return routerRoutingId;
    }

    public RoutingId pubSubRoutingId() {
        return pubSubRoutingId;
    }

    public RoutingId nodeRoutingId() {
        if (routerRoutingId != null) {
            return routerRoutingId;
        }
        return pubSubRoutingId;
    }

    public RoutingId entrySpotRoutingId() {
        return entrySpotRoutingId;
    }

    public boolean routerEnabled() {
        return routerEnabled;
    }

    public boolean pubSubEnabled() {
        return pubSubEnabled;
    }

    public Map<String, SpotPublisherClientRegistration> attachedSpotPublisherClients() {
        return Map.copyOf(attachedSpotPublisherClients);
    }

    public Map<String, SpotChannelClientRegistration> attachedChannelClients() {
        return Map.copyOf(attachedChannelClients);
    }

    public Map<String, SpotRouteChannelAcceptanceRegistration> acceptedSpotRouteChannels() {
        return Map.copyOf(acceptedSpotRouteChannels);
    }

    public List<RouterManualConnection> routerManualConnections() {
        return List.copyOf(routerManualConnections);
    }

    public List<String> pubSubManualConnections() {
        return List.copyOf(pubSubManualConnections);
    }

    void enableRouter() {
        routerEnabled = true;
    }

    void enablePubSub() {
        pubSubEnabled = true;
    }

    void setRouterBind(String endpoint) {
        enableRouter();
        routerBind = requireEndpoint(endpoint, "router endpoint");
    }

    void setPubBind(String endpoint) {
        enablePubSub();
        pubBind = requireEndpoint(endpoint, "pub endpoint");
    }

    void setRouterRoutingId(RoutingId routingId) {
        if (routingId == null) {
            throw new ZLinkConfigurationException("spot router routing id is required");
        }
        if (routerRoutingId != null && !routerRoutingId.equals(routingId)) {
            throw new ZLinkConfigurationException(
                "spot router routing id is already configured: " + nodeName);
        }
        routerRoutingId = routingId;
    }

    void setPubSubRoutingId(RoutingId routingId) {
        if (routingId == null) {
            throw new ZLinkConfigurationException("spot pub/sub routing id is required");
        }
        if (pubSubRoutingId != null && !pubSubRoutingId.equals(routingId)) {
            throw new ZLinkConfigurationException(
                "spot pub/sub routing id is already configured: " + nodeName);
        }
        pubSubRoutingId = routingId;
    }

    ZLinkEntrySpotOptions entrySpotOptions() {
        return new EntrySpotOptions();
    }

    SpotPublisherClientRegistration attachSpotPublisherClient(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException(
                "attached SPOT publisher channel name is required");
        }
        enableRouter();
        return attachedSpotPublisherClients.computeIfAbsent(
            channelName,
            SpotPublisherClientRegistration::new);
    }

    SpotChannelClientRegistration attachChannelClient(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException(
                "attached client/server channel name is required");
        }
        enableRouter();
        return attachedChannelClients.computeIfAbsent(
            channelName,
            SpotChannelClientRegistration::new);
    }

    SpotRouteChannelAcceptanceRegistration acceptSpotRoutesFromChannel(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new ZLinkConfigurationException(
                "accepted SPOT route channel name is required");
        }
        SpotRouteChannelAcceptanceRegistration acceptance =
            new SpotRouteChannelAcceptanceRegistration(channelName);
        if (acceptedSpotRouteChannels.putIfAbsent(channelName, acceptance) != null) {
            throw new ZLinkConfigurationException(
                "duplicate accepted SPOT route channel on node: "
                    + nodeName + "/" + channelName);
        }
        return acceptance;
    }

    void addRouterManualConnection(String endpoint) {
        enableRouter();
        routerManualConnections.add(new RouterManualConnection(
            null,
            requireEndpoint(endpoint, "router manual endpoint")));
    }

    void addRouterManualConnection(RoutingId peerRoutingId, String endpoint) {
        if (peerRoutingId == null || peerRoutingId.size() == 0) {
            throw new ZLinkConfigurationException("router manual peer routing id is required");
        }
        enableRouter();
        routerManualConnections.add(new RouterManualConnection(
            peerRoutingId,
            requireEndpoint(endpoint, "router manual endpoint")));
    }

    void addPubSubManualConnection(String endpoint) {
        enablePubSub();
        pubSubManualConnections.add(requireEndpoint(endpoint, "peer pub endpoint"));
    }

    void addSpotFactory(Class<? extends ZLinkSpot<?>> spotType) {
        if (spotType == null) {
            throw new ZLinkConfigurationException("spot factory type is required");
        }
        enableRouter();
        spotFactories.add(spotType);
    }

    void addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
        if (entrySpotType == null) {
            throw new ZLinkConfigurationException("entry spot type is required");
        }
        enableRouter();
        entrySpots.add(entrySpotType);
    }

    private final class EntrySpotOptions implements ZLinkEntrySpotOptions {
        @Override
        public RoutingId routingId() {
            return entrySpotRoutingId;
        }

        @Override
        public void setRoutingId(RoutingId routingId) {
            if (routingId == null) {
                throw new ZLinkConfigurationException(
                    "entry spot routing id is required");
            }
            entrySpotRoutingId = routingId;
        }
    }

    public void validate() {
        Set<Class<? extends ZLinkSpot<?>>> spotTypes = new HashSet<>();
        for (Class<? extends ZLinkSpot<?>> spotFactory : spotFactories) {
            if (!spotTypes.add(spotFactory)) {
                throw new ZLinkConfigurationException(
                    "duplicate spot factory type on node: " + nodeName);
            }
        }
        if (entrySpots.size() > 1) {
            throw new ZLinkConfigurationException(
                "spot node registers multiple entry spots: " + nodeName);
        }
        if (routerRoutingId != null
            && pubSubRoutingId != null
            && !routerRoutingId.equals(pubSubRoutingId)
            && routerBind == null
            && pubBind == null
            && routerManualConnections.isEmpty()
            && pubSubManualConnections.isEmpty()) {
            throw new ZLinkConfigurationException(
                "spot router and pub/sub routing ids must match on node: " + nodeName);
        }
        if (!attachedSpotPublisherClients.isEmpty() && !pubSubEnabled) {
            throw new ZLinkConfigurationException(
                "spot publisher client requires pub/sub on node: " + nodeName);
        }
    }

    private static String requireEndpoint(String endpoint, String label) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException(label + " is required");
        }
        return endpoint;
    }

    public record RouterManualConnection(RoutingId peerRoutingId, String endpoint) {
    }
}
