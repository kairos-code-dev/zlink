package systems.zlink.framework.runtime.mesh;

import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.LinkedHashSet;
import java.util.Set;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRelocationAdapter;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.configuration.ZLinkMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMeshNodeBuilder;
import systems.zlink.framework.configuration.ZLinkMeshObjectClientBuilder;
import systems.zlink.framework.configuration.ZLinkMeshObjectRoleBuilder;
import systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder;
import systems.zlink.framework.configuration.ZLinkMeshNodeSocketConfig;
import systems.zlink.framework.configuration.ZLinkActorFactoryOptions;
import systems.zlink.framework.configuration.ZLinkInstanceSpotFactoryOptions;
import systems.zlink.framework.configuration.ZLinkUserSpotFactoryOptions;
import systems.zlink.framework.configuration.ZLinkMeshPeerConnection;
import systems.zlink.framework.configuration.ZLinkMeshPeerConnections;
import systems.zlink.framework.configuration.ZLinkSpotPublisherConfig;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkInstanceSpot;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotRelocationAdapter;

public final class MeshNodeRegistration implements ZLinkMeshNodeBuilder {
    private final String meshName;
    private final Map<String, Channel> channels = new LinkedHashMap<>();
    private final List<Peer> peers = new ArrayList<>();
    private final List<DispatchHandler> routeHandlers = new ArrayList<>();
    private final List<Class<? extends ZLinkSpot<?>>> spotFactories = new ArrayList<>();
    private final List<Class<? extends ZLinkEntrySpot<?>>> entrySpots = new ArrayList<>();
    private final Map<String, Class<? extends ZLinkActorFactory>> actorFactories =
        new LinkedHashMap<>();
    private final Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> transferAdapters =
        new LinkedHashMap<>();
    private final Map<String, RelocatableSpotFactory<?>> relocatableSpotFactories =
        new LinkedHashMap<>();
    private final Map<String, RelocatableInstanceSpotFactory<?>> relocatableInstanceSpotFactories =
        new LinkedHashMap<>();
    private final Map<String, RelocatableActorFactory<?>> relocatableActorFactories =
        new LinkedHashMap<>();
    private final ObjectRoles objectRoles = new ObjectRoles();
    private final RouterSocketConfig routerSocket = new RouterSocketConfig();
    private final SpotPublisherConfig spotPublisher = new SpotPublisherConfig();
    private String bindEndpoint;
    private String bindHost;
    private String advertiseHost;
    private Integer listenPort;
    private RoutingId routingId;
    private String routingIdPrefix;
    private String entrySpotId;
    private int placementWeight = 100;
    private int actorCapacity;
    private int spotCapacity;
    private int activationConcurrency = 128;
    private Duration defaultRequestTimeout;

    public MeshNodeRegistration(String meshName) {
        this(meshName, "127.0.0.1", null);
    }

    public MeshNodeRegistration(
        String meshName,
        String bindHost,
        String advertiseHost) {
        this.meshName = requireText(meshName, "mesh name");
        this.bindHost = requireText(bindHost, "bind host");
        this.advertiseHost = advertiseHost;
    }

    public String meshName() {
        return meshName;
    }

    public String bindEndpoint() {
        return bindEndpoint;
    }

    public synchronized RoutingId routingId() {
        if (routingId == null) {
            routingId = RoutingId.from(
                routingIdPrefix() + "-" + java.util.UUID.randomUUID());
        }
        return routingId;
    }

    public int placementWeight() {
        return placementWeight;
    }

    public int actorCapacity() {
        return actorCapacity;
    }

    public int spotCapacity() {
        return spotCapacity;
    }

    public int activationConcurrency() {
        return activationConcurrency;
    }

    public String routingIdPrefix() {
        return routingIdPrefix == null ? meshName : routingIdPrefix;
    }

    public String entrySpotId() {
        return ensureEntrySpotId();
    }

    public List<String> channelNames() {
        return List.copyOf(channels.keySet());
    }

    public Map<String, Integer> channelWeights() {
        Map<String, Integer> weights = new LinkedHashMap<>();
        channels.forEach((name, channel) -> weights.put(name, channel.weight));
        return Map.copyOf(weights);
    }

    public List<Peer> peers() {
        return List.copyOf(peers);
    }

    public List<DispatchHandler> nodeHandlers() {
        return List.copyOf(routeHandlers);
    }

    public Map<String, List<DispatchHandler>> channelHandlers() {
        Map<String, List<DispatchHandler>> result = new LinkedHashMap<>();
        channels.forEach((name, channel) ->
            result.put(name, List.copyOf(channel.handlers)));
        return Map.copyOf(result);
    }

    public Map<String, List<String>> channelHandlerGroups() {
        Map<String, List<String>> result = new LinkedHashMap<>();
        channels.forEach((name, channel) ->
            result.put(name, List.copyOf(channel.handlerGroups)));
        return Map.copyOf(result);
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
        return Map.copyOf(transferAdapters);
    }

    public Map<String, RelocatableSpotFactory<?>> relocatableSpotFactories() {
        return Map.copyOf(relocatableSpotFactories);
    }

    public Map<String, RelocatableInstanceSpotFactory<?>> relocatableInstanceSpotFactories() {
        return Map.copyOf(relocatableInstanceSpotFactories);
    }

    public Map<String, RelocatableActorFactory<?>> relocatableActorFactories() {
        return Map.copyOf(relocatableActorFactories);
    }

    public boolean objectRoleEnabled() {
        return objectRoles.client || objectRoles.server;
    }

    public boolean objectServer() {
        return objectRoles.server;
    }

    public boolean requiresRelocationStore() {
        return java.util.stream.Stream.of(
                relocatableSpotFactories.values().stream()
                    .map(RelocatableSpotFactory::relocationPolicy),
                relocatableInstanceSpotFactories.values().stream()
                    .map(RelocatableInstanceSpotFactory::relocationPolicy),
                relocatableActorFactories.values().stream()
                    .map(RelocatableActorFactory::relocationPolicy))
            .flatMap(java.util.function.Function.identity())
            .anyMatch(policy -> !(policy instanceof ZLinkRelocationPolicy.Disabled<?>));
    }

    public Duration defaultRequestTimeout() {
        return defaultRequestTimeout;
    }

    public Set<Class<?>> applicationTypes() {
        Set<Class<?>> types = new LinkedHashSet<>();
        routeHandlers.forEach(handler -> types.add(handler.handlerType()));
        channels.values().forEach(channel ->
            channel.handlers.forEach(handler -> types.add(handler.handlerType())));
        types.addAll(spotFactories);
        types.addAll(entrySpots);
        types.addAll(actorFactories.values());
        types.addAll(transferAdapters.values());
        relocatableSpotFactories.values().forEach(factory -> {
            types.add(factory.spotType());
            addRelocationAdapterType(types, factory.relocationPolicy());
        });
        relocatableInstanceSpotFactories.values().forEach(factory -> {
            types.add(factory.spotType());
            addRelocationAdapterType(types, factory.relocationPolicy());
        });
        relocatableActorFactories.values().forEach(factory -> {
            types.add(factory.actorType());
            types.add(factory.factoryType());
            addRelocationAdapterType(types, factory.relocationPolicy());
        });
        return Set.copyOf(types);
    }

    private static void addRelocationAdapterType(
        Set<Class<?>> types,
        ZLinkRelocationPolicy<?> policy) {
        if (policy instanceof ZLinkRelocationPolicy.Snapshot<?> snapshot) {
            types.add(snapshot.adapterClass());
        }
    }

    @Override
    public ZLinkMeshChannelBuilder channelName(String channelName) {
        String name = requireText(channelName, "channel name");
        Channel channel = new Channel(name);
        if (channels.putIfAbsent(name, channel) != null) {
            throw new ZLinkConfigurationException(
                "duplicate channel name on RouteMesh " + meshName + ": " + name);
        }
        return channel;
    }

    @Override
    public ZLinkMeshNodeBuilder listen(String endpoint) {
        bindEndpoint = requireText(endpoint, "MeshNode listen endpoint");
        listenPort = null;
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder listen() {
        return listen(0);
    }

    @Override
    public ZLinkMeshNodeBuilder listen(int port) {
        if (port < 0 || port > 65_535) {
            throw new ZLinkConfigurationException(
                "MeshNode listen port must be between 0 and 65535");
        }
        listenPort = port;
        updateDefaultEndpoint();
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setBindHost(String host) {
        bindHost = requireText(host, "bind host");
        updateDefaultEndpoint();
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setAdvertiseHost(String host) {
        advertiseHost = requireText(host, "advertise host");
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setRoutingId(RoutingId value) {
        routingId = Objects.requireNonNull(value, "routingId");
        return this;
    }

    public String advertisedEndpoint(String actualEndpoint) {
        if (advertiseHost == null || actualEndpoint == null
            || !actualEndpoint.startsWith("tcp://")) {
            return actualEndpoint;
        }
        int colon = actualEndpoint.lastIndexOf(':');
        return colon < "tcp://".length()
            ? actualEndpoint
            : "tcp://" + advertiseHost + actualEndpoint.substring(colon);
    }

    private void updateDefaultEndpoint() {
        if (listenPort != null) {
            bindEndpoint = "tcp://" + bindHost + ":" + listenPort;
        }
    }

    @Override
    public ZLinkMeshNodeBuilder setRoutingIdPrefix(String value) {
        routingIdPrefix = requireText(value, "routing ID prefix");
        routingId = null;
        entrySpotId = null;
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setPlacementWeight(int value) {
        if (value < 0 || value > 10_000) {
            throw new ZLinkConfigurationException(
                "placement weight must be in range 0..10000");
        }
        placementWeight = value;
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setActorCapacity(int maxActors) {
        if (maxActors < 0) {
            throw new ZLinkConfigurationException(
                "Actor capacity must not be negative.");
        }
        actorCapacity = maxActors;
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setSpotCapacity(int maxSpots) {
        if (maxSpots < 0) {
            throw new ZLinkConfigurationException(
                "Spot capacity must not be negative.");
        }
        spotCapacity = maxSpots;
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder setActivationConcurrency(
        int maxConcurrentActivations) {
        if (maxConcurrentActivations <= 0) {
            throw new ZLinkConfigurationException(
                "Activation concurrency must be positive.");
        }
        activationConcurrency = maxConcurrentActivations;
        return this;
    }

    @Override
    public ZLinkMeshNodeSocketConfig configureRouterSocket() {
        return routerSocket;
    }

    @Override
    public ZLinkSpotPublisherConfig configureSpotPublisher() {
        return spotPublisher;
    }

    @Override
    public ZLinkMeshPeerConnections peerConnections() {
        return new PeerConnections();
    }

    @Override
    public ZLinkMeshNodeBuilder setDefaultRequestTimeout(Duration timeout) {
        defaultRequestTimeout = requirePositive(timeout, "default request timeout");
        return this;
    }

    @Override
    public ZLinkMeshObjectRoleBuilder objects() {
        return objectRoles;
    }

    @Override
    public <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage>
    ZLinkMeshNodeBuilder addRouteSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType) {
        routeHandlers.add(new DispatchHandler(
            Objects.requireNonNull(handlerType, "handlerType"),
            Objects.requireNonNull(messageType, "messageType"),
            null));
        return this;
    }

    @Override
    public <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    ZLinkMeshNodeBuilder addRouteRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType) {
        routeHandlers.add(new DispatchHandler(
            Objects.requireNonNull(handlerType, "handlerType"),
            Objects.requireNonNull(requestType, "requestType"),
            Objects.requireNonNull(replyType, "replyType")));
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType) {
        spotFactories.add(Objects.requireNonNull(spotType, "spotType"));
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder addEntrySpot(
        Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
        entrySpots.add(Objects.requireNonNull(entrySpotType, "entrySpotType"));
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType) {
        putUnique(actorFactories, actorType, factoryType, "actor factory");
        return this;
    }

    @Override
    public ZLinkMeshNodeBuilder addActorTransferAdapter(
        String actorType,
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType) {
        putUnique(transferAdapters, actorType, adapterType, "actor transfer adapter");
        return this;
    }

    public void validate() {
        if (bindEndpoint == null) {
            throw new ZLinkConfigurationException(
                "MeshNode listen endpoint is required: " + meshName);
        }
        routingId();
        if (entrySpots.size() > 1) {
            throw new ZLinkConfigurationException(
                "MeshNode registers multiple entry spots: " + meshName);
        }
        Set<Class<? extends ZLinkSpot<?>>> spotTypes = new LinkedHashSet<>();
        for (Class<? extends ZLinkSpot<?>> spotFactory : spotFactories) {
            if (!spotTypes.add(spotFactory)) {
                throw new ZLinkConfigurationException(
                    "duplicate spot factory type on MeshNode: " + meshName);
            }
        }
        for (String actorType : transferAdapters.keySet()) {
            if (!actorFactories.containsKey(actorType)) {
                throw new ZLinkConfigurationException(
                    "actor transfer adapter requires an actor factory: " + actorType);
            }
        }
        relocatableSpotFactories.values().forEach(factory ->
            validateRelocationPolicy(
                factory.spotType(),
                factory.relocationPolicy(),
                ZLinkSpotRelocationAdapter.class,
                "Spot"));
        relocatableInstanceSpotFactories.values().forEach(factory ->
            validateRelocationPolicy(
                factory.spotType(),
                factory.relocationPolicy(),
                ZLinkSpotRelocationAdapter.class,
                "Instance Spot"));
        relocatableActorFactories.values().forEach(factory ->
            validateRelocationPolicy(
                factory.actorType(),
                factory.relocationPolicy(),
                ZLinkActorRelocationAdapter.class,
                "Actor"));
    }

    private static void validateRelocationPolicy(
        Class<?> objectType,
        ZLinkRelocationPolicy<?> policy,
        Class<?> adapterContract,
        String label) {
        Objects.requireNonNull(policy, "relocationPolicy");
        if (!(policy instanceof ZLinkRelocationPolicy.Snapshot<?> snapshot)) {
            return;
        }
        Class<?> adapterType = Objects.requireNonNull(
            snapshot.adapterClass(),
            "snapshot adapterClass");
        if (!adapterContract.isAssignableFrom(adapterType)) {
            throw new ZLinkConfigurationException(
                label + " snapshot adapter must implement "
                    + adapterContract.getSimpleName() + ": "
                    + adapterType.getName());
        }
        if (!ZLinkRelocationAdapterTypeMatcher.matches(
            adapterType,
            adapterContract,
            objectType)) {
            throw new ZLinkConfigurationException(
                label + " snapshot adapter type does not match "
                    + objectType.getName() + ": " + adapterType.getName());
        }
    }

    private final class ObjectRoles
        implements ZLinkMeshObjectRoleBuilder,
            ZLinkMeshObjectClientBuilder,
            ZLinkMeshObjectServerBuilder {
        private boolean client;
        private boolean server;

        @Override
        public ZLinkMeshObjectClientBuilder client() {
            client = true;
            return this;
        }

        @Override
        public ZLinkMeshObjectServerBuilder server() {
            client = true;
            server = true;
            ensureEntrySpotId();
            return this;
        }

        @Override
        public ZLinkMeshObjectServerBuilder addEntrySpot(
            Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
            MeshNodeRegistration.this.addEntrySpot(entrySpotType);
            return this;
        }

        @Override
        public <TSpot extends ZLinkSpot<?>>
        ZLinkMeshObjectServerBuilder addSpotFactory(
            String stableType,
            Class<TSpot> spotType,
            ZLinkUserSpotFactoryOptions options,
            ZLinkRelocationPolicy<TSpot> relocationPolicy) {
            String type = requireStableType(stableType);
            RelocatableSpotFactory<TSpot> value = new RelocatableSpotFactory<>(
                type,
                Objects.requireNonNull(spotType, "spotType"),
                Objects.requireNonNull(options, "options"),
                Objects.requireNonNull(relocationPolicy, "relocationPolicy"));
            putUnique(relocatableSpotFactories, type, value, "Spot stable type");
            spotFactories.add(spotType);
            return this;
        }

        @Override
        public <TSpot extends ZLinkInstanceSpot>
        ZLinkMeshObjectServerBuilder addInstanceSpotFactory(
            String stableType,
            Class<TSpot> spotType,
            ZLinkInstanceSpotFactoryOptions options,
            ZLinkRelocationPolicy<TSpot> relocationPolicy) {
            String type = requireStableType(stableType);
            RelocatableInstanceSpotFactory<TSpot> value =
                new RelocatableInstanceSpotFactory<>(
                    type,
                    Objects.requireNonNull(spotType, "spotType"),
                    Objects.requireNonNull(options, "options"),
                    Objects.requireNonNull(relocationPolicy, "relocationPolicy"));
            putUnique(
                relocatableInstanceSpotFactories,
                type,
                value,
                "Instance Spot stable type");
            return this;
        }

        @Override
        public <TActor extends ZLinkActor>
        ZLinkMeshObjectServerBuilder addActorFactory(
            String stableType,
            Class<TActor> actorType,
            Class<? extends ZLinkActorFactory> factoryType,
            ZLinkActorFactoryOptions options,
            ZLinkRelocationPolicy<TActor> relocationPolicy) {
            String type = requireStableType(stableType);
            RelocatableActorFactory<TActor> value = new RelocatableActorFactory<>(
                type,
                Objects.requireNonNull(actorType, "actorType"),
                Objects.requireNonNull(factoryType, "factoryType"),
                Objects.requireNonNull(options, "options"),
                Objects.requireNonNull(relocationPolicy, "relocationPolicy"));
            putUnique(relocatableActorFactories, type, value, "Actor stable type");
            putUnique(actorFactories, type, factoryType, "actor factory");
            return this;
        }
    }

    private String ensureEntrySpotId() {
        if (entrySpotId == null) {
            entrySpotId = routingIdPrefix()
                + "-entry-"
                + java.util.UUID.randomUUID().toString()
                    .toLowerCase(java.util.Locale.ROOT);
        }
        return entrySpotId;
    }

    private static String requireStableType(String value) {
        return requireText(value, "stable type");
    }

    private static String requireText(String value, String label) {
        if (value == null || value.isBlank()) {
            throw new ZLinkConfigurationException(label + " is required");
        }
        return value;
    }

    private static Duration requirePositive(Duration value, String label) {
        Objects.requireNonNull(value, label);
        if (value.isNegative() || value.isZero()) {
            throw new ZLinkConfigurationException(label + " must be positive");
        }
        return value;
    }

    private static <T> void putUnique(
        Map<String, T> values,
        String key,
        T value,
        String label) {
        String name = requireText(key, "actor type");
        if (values.putIfAbsent(name, Objects.requireNonNull(value, "value")) != null) {
            throw new ZLinkConfigurationException("duplicate " + label + ": " + name);
        }
    }

    public record Peer(String endpoint, RoutingId expectedRoutingId) {
    }

    public record DispatchHandler(
        Class<?> handlerType,
        Class<?> messageType,
        Class<?> replyType) {
        public boolean request() {
            return replyType != null;
        }
    }

    public record RelocatableSpotFactory<TSpot extends ZLinkSpot<?>>(
        String stableType,
        Class<TSpot> spotType,
        ZLinkUserSpotFactoryOptions options,
        ZLinkRelocationPolicy<TSpot> relocationPolicy) {
    }

    public record RelocatableInstanceSpotFactory<TSpot extends ZLinkInstanceSpot>(
        String stableType,
        Class<TSpot> spotType,
        ZLinkInstanceSpotFactoryOptions options,
        ZLinkRelocationPolicy<TSpot> relocationPolicy) {
    }

    public record RelocatableActorFactory<TActor extends ZLinkActor>(
        String stableType,
        Class<TActor> actorType,
        Class<? extends ZLinkActorFactory> factoryType,
        ZLinkActorFactoryOptions options,
        ZLinkRelocationPolicy<TActor> relocationPolicy) {
    }

    private final class PeerConnections implements ZLinkMeshPeerConnections {
        @Override
        public void connect(String endpoint) {
            peers.add(new Peer(requireText(endpoint, "peer endpoint"), null));
        }

        @Override
        public void connect(RoutingId expectedRoutingId, String endpoint) {
            peers.add(new Peer(
                requireText(endpoint, "peer endpoint"),
                Objects.requireNonNull(expectedRoutingId, "expectedRoutingId")));
        }

        @Override
        public void disconnect(String endpoint) {
            String target = requireText(endpoint, "peer endpoint");
            peers.removeIf(peer -> peer.endpoint().equals(target));
        }

        @Override
        public List<ZLinkMeshPeerConnection> listConnections() {
            return peers.stream()
                .map(peer -> new ZLinkMeshPeerConnection(
                    peer.endpoint(),
                    Optional.ofNullable(peer.expectedRoutingId())))
                .toList();
        }
    }

    private final class Channel implements ZLinkMeshChannelBuilder {
        private final String name;
        private final List<String> handlerGroups = new ArrayList<>();
        private final List<DispatchHandler> handlers = new ArrayList<>();
        private int weight = 100;

        private Channel(String name) {
            this.name = name;
        }

        @Override
        public ZLinkMeshChannelBuilder setWeight(int value) {
            if (value < 0 || value > 10_000) {
                throw new ZLinkConfigurationException(
                    "channel weight must be in 0..10000");
            }
            weight = value;
            return this;
        }

        @Override
        public ZLinkMeshChannelBuilder addHandlerGroup(String groupName) {
            handlerGroups.add(requireText(groupName, "handler group"));
            return this;
        }

        @Override
        public <THandler extends ZLinkSendHandler<TMessage>, TMessage>
        ZLinkMeshChannelBuilder addSendHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType) {
            handlers.add(new DispatchHandler(
                Objects.requireNonNull(handlerType, "handlerType"),
                Objects.requireNonNull(messageType, "messageType"),
                null));
            return this;
        }

        @Override
        public <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
        ZLinkMeshChannelBuilder addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType) {
            handlers.add(new DispatchHandler(
                Objects.requireNonNull(handlerType, "handlerType"),
                Objects.requireNonNull(requestType, "requestType"),
                Objects.requireNonNull(replyType, "replyType")));
            return this;
        }
    }

    private static final class RouterSocketConfig implements ZLinkMeshNodeSocketConfig {
        private long maxMessageSize;
        private int sendHighWaterMark = 1000;
        private int receiveHighWaterMark = 1000;
        private Duration receiveTimeout;
        private Duration sendTimeout;

        @Override public long maxMessageSize() { return maxMessageSize; }
        @Override public void setMaxMessageSize(long value) { maxMessageSize = value; }
        @Override public int sendHighWaterMark() { return sendHighWaterMark; }
        @Override public void setSendHighWaterMark(int value) { sendHighWaterMark = value; }
        @Override public int receiveHighWaterMark() { return receiveHighWaterMark; }
        @Override public void setReceiveHighWaterMark(int value) { receiveHighWaterMark = value; }
        @Override public Optional<Duration> receiveTimeout() {
            return Optional.ofNullable(receiveTimeout);
        }
        @Override public void setReceiveTimeout(Duration value) { receiveTimeout = value; }
        @Override public Optional<Duration> sendTimeout() {
            return Optional.ofNullable(sendTimeout);
        }
        @Override public void setSendTimeout(Duration value) {
            sendTimeout = value == null ? null : requireSendTimeout(value);
        }
    }

    private static final class SpotPublisherConfig implements ZLinkSpotPublisherConfig {
        private int sendHighWaterMark = 1000;
        private Duration sendTimeout;
        private Duration linger;

        @Override public int sendHighWaterMark() { return sendHighWaterMark; }
        @Override public void setSendHighWaterMark(int value) { sendHighWaterMark = value; }
        @Override public Optional<Duration> sendTimeout() {
            return Optional.ofNullable(sendTimeout);
        }
        @Override public void setSendTimeout(Duration value) {
            sendTimeout = value == null ? null : requireSendTimeout(value);
        }
        @Override public Optional<Duration> linger() { return Optional.ofNullable(linger); }
        @Override public void setLinger(Duration value) { linger = value; }
    }

    private static Duration requireSendTimeout(Duration value) {
        if (value.isZero() || value.isNegative()) {
            throw new ZLinkConfigurationException("send timeout must be positive");
        }
        long seconds = value.getSeconds();
        if (seconds > Integer.MAX_VALUE / 1000L) {
            throw new ZLinkConfigurationException(
                "send timeout must normalize to at most Integer.MAX_VALUE ms");
        }
        long millis = seconds * 1000L
            + (value.getNano() + 999_999L) / 1_000_000L;
        if (millis > Integer.MAX_VALUE) {
            throw new ZLinkConfigurationException(
                "send timeout must normalize to at most Integer.MAX_VALUE ms");
        }
        return value;
    }
}
