package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.configuration.ZLinkEntrySpotOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy;

public final class SpotNodeRegistration {
    private final String meshName;
    private final String nodeName;
    private final List<Class<? extends ZLinkSpot<?>>> spotFactories = new ArrayList<>();
    private final List<Class<? extends ZLinkEntrySpot<?>>> entrySpots = new ArrayList<>();
    private final Map<String, Class<? extends ZLinkActorFactory>> actorFactories =
        new LinkedHashMap<>();
    private final Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> actorTransferAdapters =
        new LinkedHashMap<>();
    private final List<RouterManualConnection> routerManualConnections = new ArrayList<>();
    private final List<String> pubSubManualConnections = new ArrayList<>();
    private boolean routerEnabled;
    private boolean pubSubEnabled;
    private String routerBind;
    private String pubBind;
    private RoutingId routingId;
    private RoutingId entrySpotRoutingId;
    private ZLinkSpotDrainPolicy drainPolicy = ZLinkSpotDrainPolicy.DRAIN_NATURAL;

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

    public Map<String, Class<? extends ZLinkActorFactory>> actorFactories() {
        return Map.copyOf(actorFactories);
    }

    public Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> actorTransferAdapters() {
        return Map.copyOf(actorTransferAdapters);
    }

    public String routerBind() {
        return routerBind;
    }

    public String pubBind() {
        return pubBind;
    }

    public RoutingId nodeRoutingId() {
        return routingId;
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

    public ZLinkSpotDrainPolicy drainPolicy() {
        return drainPolicy;
    }

    void setDrainPolicy(ZLinkSpotDrainPolicy drainPolicy) {
        this.drainPolicy = java.util.Objects.requireNonNull(drainPolicy, "drainPolicy");
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

    void setRoutingId(RoutingId routingId) {
        if (routingId == null) {
            throw new ZLinkConfigurationException("spot node routing id is required");
        }
        if (this.routingId != null && !this.routingId.equals(routingId)) {
            throw new ZLinkConfigurationException(
                "spot node routing id is already configured: " + nodeName);
        }
        this.routingId = routingId;
    }

    ZLinkEntrySpotOptions entrySpotOptions() {
        return new EntrySpotOptions();
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

    void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType) {
        if (factoryType == null) {
            throw new ZLinkConfigurationException("actor factory type is required");
        }
        String type = requireEndpoint(actorType, "actor type");
        if (actorFactories.putIfAbsent(type, factoryType) != null) {
            throw new ZLinkConfigurationException("duplicate actor type on node: " + type);
        }
        enableRouter();
    }

    void addActorTransferAdapter(
        String actorType,
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType) {
        if (adapterType == null) {
            throw new ZLinkConfigurationException("actor transfer adapter type is required");
        }
        String type = requireEndpoint(actorType, "actor type");
        if (actorTransferAdapters.putIfAbsent(type, adapterType) != null) {
            throw new ZLinkConfigurationException(
                "duplicate actor transfer adapter on node: " + type);
        }
        enableRouter();
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
        for (String actorType : actorTransferAdapters.keySet()) {
            if (!actorFactories.containsKey(actorType)) {
                throw new ZLinkConfigurationException(
                    "actor transfer adapter requires an actor factory on node: " + actorType);
            }
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
