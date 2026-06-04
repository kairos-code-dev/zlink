package systems.zlink.framework.runtime.spots;

import systems.zlink.framework.runtime.backend.*;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorChangeKind;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class ZLinkSpotRuntime implements ZLinkSpotManager, ZLinkChannelRuntime.SpotRelayIngress, AutoCloseable {
    private final ZLinkBackendContext context;
    private final List<ZLinkBackendSpotNode> nodes = new ArrayList<>();
    private final List<ZLinkBackendDealerSocket> attachedChannelDealers = new ArrayList<>();
    private final List<ZLinkBackendDiscovery> attachedChannelDiscoveries = new ArrayList<>();
    private final Map<String, ZLinkBackendSpotNode> nodesByName = new HashMap<>();
    private final Map<String, ZLinkBackendDiscovery> spotDiscoveriesByMesh = new HashMap<>();
    private final Map<String, ZLinkBackendSpotNode> publisherNodesByChannel = new HashMap<>();
    private final Map<String, ZLinkBackendSpot> publisherSpotsByChannel = new HashMap<>();
    private final Set<Class<? extends ZLinkSpot>> registeredSpotTypes = new HashSet<>();
    private final Map<RoutingId, SpotActivation> spots = new HashMap<>();
    private final List<EntrySpotActivation> entrySpots = new ArrayList<>();
    private final ZLinkBackendSpotNode primaryNode;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final Map<String, SpotActorJoinHandlerRegistration> actorJoinHandlers;
    private final Map<String, SpotActorPacketHandlerRegistration> actorPacketHandlers;
    private final List<SpotActorLifecycleHandlerRegistration> actorJoinedHandlers;
    private final List<SpotActorLifecycleHandlerRegistration> actorLeftHandlers;
    private final List<SpotActorLifecycleHandlerRegistration> actorDisconnectedHandlers;
    private final Duration defaultTimeout;
    private final ZLinkChannelRuntime channels;
    private ZLinkActorRuntime actorRuntime;
    private final Map<String, SpotNodeRegistration> acceptedRouteNodesByChannel = new HashMap<>();
    private final List<String> egressChannels = new ArrayList<>();
    private final List<String> routeMeshChannels = new ArrayList<>();
    private final ThreadLocal<DefaultSpotOutbound> currentOutbound = new ThreadLocal<>();
    private final ScheduledExecutorService timerExecutor = Executors.newScheduledThreadPool(1, task -> {
        Thread thread = new Thread(task, "zlink-java-spot-timer");
        thread.setDaemon(true);
        return thread;
    });

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration) {
        this(backendFactory, adapterOptions, registration, null);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels) {
        this(backendFactory, adapterOptions, registration, channels, ZLinkHandlerFactory.reflection());
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkHandlerFactory handlerFactory) {
        this(
            backendFactory,
            adapterOptions,
            registration,
            channels,
            new ZLinkStringMessageSerializer(),
            handlerFactory);
    }

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration,
        ZLinkChannelRuntime channels,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        if (registration.spotNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one SpotNode is required");
        }
        this.channels = channels;
        this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
        this.handlerFactory = handlerFactory;
        ZLinkScannedHandlerCatalog handlerCatalog =
            ZLinkHandlerScanner.scan(registration.handlerPackageMarkers());
        this.actorJoinHandlers = actorJoinHandlersByPacket(handlerCatalog);
        this.actorPacketHandlers = actorPacketHandlersByPacket(handlerCatalog);
        this.actorJoinedHandlers = actorJoinedHandlers(handlerCatalog);
        this.actorLeftHandlers = actorLeftHandlers(handlerCatalog);
        this.actorDisconnectedHandlers = actorDisconnectedHandlers(handlerCatalog);
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkSpotBackendAdapter spotAdapter =
            backendFactory.createSpotAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        this.defaultTimeout = registration.defaultTimeout();
        for (SpotNodeRegistration nodeRegistration : registration.spotNodes()) {
            ZLinkBackendSpotNode node =
                spotAdapter.createSpotNode(context, ZLinkBackendSpotNodeMode.ALL);
            if (nodeRegistration.routingId() != null) {
                node.setRoutingId(nodeRegistration.routingId());
            }
            if (nodeRegistration.entrySpotRoutingId() != null
                || !nodeRegistration.entrySpots().isEmpty()) {
                ZLinkBackendSpot entryBackendSpot = node.entrySpot();
                if (nodeRegistration.entrySpotRoutingId() != null) {
                    entryBackendSpot.setRoutingId(nodeRegistration.entrySpotRoutingId());
                }
                for (Class<? extends ZLinkEntrySpot> entrySpotType : nodeRegistration.entrySpots()) {
                    entrySpots.add(activateEntrySpot(
                        node.routingId(),
                        entryBackendSpot,
                        entrySpotType));
                }
            }
            if (nodeRegistration.routerBind() != null) {
                node.setRouterBind(nodeRegistration.routerBind());
            }
            if (nodeRegistration.pubBind() != null) {
                node.setPubBind(nodeRegistration.pubBind());
            }
            attachSpotMeshDiscovery(
                channelAdapter,
                registration,
                nodeRegistration,
                node);
            for (String endpoint : nodeRegistration.routerManualConnections()) {
                node.connectPeer(endpoint);
            }
            for (String endpoint : nodeRegistration.pubSubManualConnections()) {
                node.connectPeer(endpoint);
            }
            for (SpotChannelClientRegistration attached :
                nodeRegistration.attachedChannelClients().values()) {
                attachChannelClient(
                    channelAdapter,
                    registration,
                    node,
                    attached);
            }
            for (SpotRouteChannelAcceptanceRegistration acceptance :
                nodeRegistration.acceptedSpotRouteChannels().values()) {
                if (acceptedRouteNodesByChannel.putIfAbsent(
                    acceptance.channelName(),
                    nodeRegistration) != null) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT channel is accepted by multiple SPOT nodes: "
                            + acceptance.channelName());
                }
                attachAcceptedSpotRouteChannel(
                    channelAdapter,
                    registration,
                    node,
                    acceptance);
            }
            nodes.add(node);
            nodesByName.put(nodeRegistration.nodeName(), node);
            registeredSpotTypes.addAll(nodeRegistration.spotFactories());
            for (SpotPublisherClientRegistration publisher :
                nodeRegistration.attachedSpotPublisherClients().values()) {
                publisherNodesByChannel.put(publisher.channelName(), node);
                for (String endpoint : publisher.manualConnections()) {
                    node.connectPeer(endpoint);
                }
            }
        }
        for (var channel : registration.channels()) {
            if (channel.kind() == ChannelKind.ROUTE_MESH) {
                routeMeshChannels.add(channel.name());
            }
            if (channel.spotRouteEgressTarget() != null) {
                egressChannels.add(channel.name());
            }
        }
        this.primaryNode = nodes.get(0);
    }

    private static Map<String, SpotActorJoinHandlerRegistration> actorJoinHandlersByPacket(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, SpotActorJoinHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || handler.kind() != ZLinkScannedHandlerKind.ACTOR_JOIN) {
                continue;
            }
            Class<?> actorType = handler.handlerMethod().getParameterTypes()[0];
            SpotActorJoinHandlerRegistration registration =
                new SpotActorJoinHandlerRegistration(
                    handler.handlerType(),
                    handler.handlerMethod(),
                    actorType,
                    handler.messageType(),
                    handler.replyType(),
                    handler.packetName());
            if (handlers.putIfAbsent(handler.packetName(), registration) != null) {
                throw new ZLinkConfigurationException(
                    "duplicate Spot actor join handler packet: " + handler.packetName());
            }
        }
        return Map.copyOf(handlers);
    }

    private static Map<String, SpotActorPacketHandlerRegistration> actorPacketHandlersByPacket(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        Map<String, SpotActorPacketHandlerRegistration> handlers = new HashMap<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || (handler.kind() != ZLinkScannedHandlerKind.ACTOR_SEND
                    && handler.kind() != ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                continue;
            }
            Class<?> actorType = handler.handlerMethod().getParameterTypes()[0];
            SpotActorPacketHandlerRegistration registration =
                new SpotActorPacketHandlerRegistration(
                    handler.handlerType(),
                    handler.handlerMethod(),
                    actorType,
                    handler.messageType(),
                    handler.replyType(),
                    handler.packetName(),
                    handler.kind());
            if (handlers.putIfAbsent(handler.packetName(), registration) != null) {
                throw new ZLinkConfigurationException(
                    "duplicate Spot actor packet handler packet: " + handler.packetName());
            }
        }
        return Map.copyOf(handlers);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorJoinedHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        return actorLifecycleHandlers(handlerCatalog, ZLinkScannedHandlerKind.ACTOR_JOINED);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorLeftHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        return actorLifecycleHandlers(handlerCatalog, ZLinkScannedHandlerKind.ACTOR_LEFT);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorDisconnectedHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog) {
        return actorLifecycleHandlers(handlerCatalog, ZLinkScannedHandlerKind.ACTOR_DISCONNECTED);
    }

    private static List<SpotActorLifecycleHandlerRegistration> actorLifecycleHandlers(
        ZLinkScannedHandlerCatalog handlerCatalog,
        ZLinkScannedHandlerKind kind) {
        List<SpotActorLifecycleHandlerRegistration> handlers = new ArrayList<>();
        for (ZLinkScannedHandler handler : handlerCatalog.handlers()) {
            if (handler.surface() != ZLinkScannedHandlerSurface.SPOT
                || handler.kind() != kind) {
                continue;
            }
            handlers.add(new SpotActorLifecycleHandlerRegistration(
                handler.handlerType(),
                handler.handlerMethod(),
                handler.messageType(),
                handler.kind()));
        }
        return List.copyOf(handlers);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType) {
        requireRegistered(spotType);
        ZLinkBackendSpot spot = primaryNode.createSpot();
        RoutingId spotRid = spot.routingId();
        if (spots.containsKey(spotRid)) {
            spot.close();
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        return activateAsync(spotType, spot)
            .thenApply(activation -> {
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(spotRid, true);
            });
        }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid)) {
            throw new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
        }
        ZLinkBackendSpot spot = primaryNode.createSpot();
        spot.setRoutingId(spotRid);
        return activateAsync(spotType, spot)
            .thenApply(activation -> {
                spots.put(spotRid, activation);
                return new ZLinkSpotCreateResult(spotRid, true);
            });
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid)) {
            return CompletableFuture.completedFuture(
                new ZLinkSpotCreateResult(spotRid, false));
        }
        return createAsync(spotType, spotRid);
    }

    @Override
    public CompletionStage<Optional<ZLinkSpotInfo>> findAsync(RoutingId spotRid) {
        requireRoutingId(spotRid);
        return CompletableFuture.completedFuture(
            spots.containsKey(spotRid)
                ? Optional.of(new ZLinkSpotInfo(spotRid))
                : Optional.empty());
    }

    @Override
    public CompletionStage<List<ZLinkSpotInfo>> listAsync() {
        return CompletableFuture.completedFuture(
            spots.keySet().stream().map(ZLinkSpotInfo::new).toList());
    }

    @Override
    public CompletionStage<Boolean> removeAsync(RoutingId spotRid) {
        requireRoutingId(spotRid);
        SpotActivation removed = spots.remove(spotRid);
        if (removed == null) {
            return CompletableFuture.completedFuture(false);
        }
        removed.close();
        return CompletableFuture.completedFuture(true);
    }

    @Override
    public void close() {
        for (EntrySpotActivation entrySpot : entrySpots) {
            entrySpot.close();
        }
        entrySpots.clear();
        for (SpotActivation spot : spots.values()) {
            spot.close();
        }
        spots.clear();
        for (ZLinkBackendSpot publisherSpot : publisherSpotsByChannel.values()) {
            publisherSpot.close();
        }
        publisherSpotsByChannel.clear();
        for (ZLinkBackendDealerSocket dealer : attachedChannelDealers) {
            dealer.close();
        }
        attachedChannelDealers.clear();
        for (ZLinkBackendDiscovery discovery : attachedChannelDiscoveries) {
            discovery.close();
        }
        attachedChannelDiscoveries.clear();
        timerExecutor.shutdownNow();
        for (ZLinkBackendSpotNode node : nodes) {
            node.close();
        }
        context.close();
    }

    public ZLinkBackendSpotNode primaryNode() {
        return primaryNode;
    }

    public void attachActorRuntime(ZLinkActorRuntime actorRuntime) {
        this.actorRuntime = actorRuntime;
        this.actorRuntime.setDisconnectedNotifier(this::notifySpotActorDisconnected);
        this.actorRuntime.setSpotResolver(this::spotFor);
    }

    public Map<String, ZLinkBackendSpotNode> nodesByName() {
        return Map.copyOf(nodesByName);
    }

    public ZLinkSpotOutbound outbound() {
        return new AmbientSpotOutbound();
    }

    public ZLinkSpotPublisherClient publisherClient() {
        return new DefaultSpotPublisherClient();
    }

    public ZLinkSpotRemoteAddress resolveRegistrySpotRemoteAddress(
        String namespaceName,
        String configuredRouterChannelId,
        RoutingId spotRid) {
        requireRoutingId(spotRid);
        String routerChannelId = resolveRouterChannelId(configuredRouterChannelId);
        ZLinkBackendDiscovery discovery = resolveSpotDiscovery(namespaceName);
        try {
            ZLinkBackendSpotRoute route = discovery.resolveSpot(spotRid);
            return new ZLinkSpotRemoteAddress(
                routerChannelId,
                route.nodeRid(),
                route.spotRid(),
                route.spotKind() == null ? ZLinkSpotKind.INVALID : route.spotKind());
        } catch (RuntimeException ex) {
            throw new systems.zlink.framework.errors.ZLinkFrameworkException(
                "SPOT route was not found for '" + spotRid + "'.",
                ex);
    }
}

    @Override
    public RoutingId resolveAcceptedSpotRouteNodeRid(String channelName) {
        SpotNodeRegistration nodeRegistration = acceptedRouteNodesByChannel.get(channelName);
        if (nodeRegistration == null) {
            throw new ZLinkConfigurationException(
                "routed SPOT target channel is not accepted by a SpotNode: " + channelName);
        }
        ZLinkBackendSpotNode node = nodesByName.get(nodeRegistration.nodeName());
        if (node == null) {
            throw new ZLinkConfigurationException(
                "accepted routed SPOT node is not active: " + nodeRegistration.nodeName());
        }
        return node.routingId();
    }

    @Override
    public void handleSend(
        String channelName,
        ZLinkBackendRouterSocket router,
        List<Message> relayParts) {
        RoutingId targetNodeRid = resolveAcceptedSpotRouteNodeRid(channelName);
        RoutingId targetSpotRid = ZLinkRoutedSpotRelayPackets.decodeTargetSpotRid(relayParts);
        List<Message> spotParts = ZLinkRoutedSpotRelayPackets.copySpotPayloadParts(relayParts);
        try {
            if (!router.sendToSpot(targetNodeRid, targetSpotRid, spotParts, SendFlags.NONE)) {
                throw new ZLinkConfigurationException(
                    "routed SPOT ingress channel is not ready for send: " + channelName);
            }
        } finally {
            spotParts.forEach(Message::close);
        }
    }

    @Override
    public CompletionStage<List<Message>> handleRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        List<Message> relayParts,
        Duration timeout) {
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        RoutingId targetNodeRid = resolveAcceptedSpotRouteNodeRid(channelName);
        RoutingId targetSpotRid = ZLinkRoutedSpotRelayPackets.decodeTargetSpotRid(relayParts);
        List<Message> spotParts = ZLinkRoutedSpotRelayPackets.copySpotPayloadParts(relayParts);
        try {
            if (!router.requestToSpot(targetNodeRid, targetSpotRid, spotParts, reply -> {
                try {
                    result.complete(ZLinkRoutedSpotRelayPackets.copyReplyParts(reply.parts()));
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                } finally {
                    reply.parts().forEach(Message::close);
                }
            }, SendFlags.NONE, timeout)) {
                result.completeExceptionally(new ZLinkConfigurationException(
                    "routed SPOT ingress channel is not ready for request: " + channelName));
            }
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            spotParts.forEach(Message::close);
        }
        return result;
    }

    private void requireRegistered(Class<? extends ZLinkSpot> spotType) {
        if (spotType == null) {
            throw new ZLinkConfigurationException("spot type is required");
        }
        if (!registeredSpotTypes.contains(spotType)) {
            throw new ZLinkConfigurationException(
                "spot type is not registered: " + spotType.getName());
        }
    }

    private static void requireRoutingId(RoutingId spotRid) {
        if (spotRid == null) {
            throw new ZLinkConfigurationException("spotRid is required");
        }
    }

    private CompletionStage<SpotActivation> activateAsync(
        Class<? extends ZLinkSpot> spotType,
        ZLinkBackendSpot backendSpot) {
        DefaultSpotContext spotContext =
            new DefaultSpotContext(primaryNode.routingId(), backendSpot);
        ZLinkSpot spot = tryCreateSpot(spotType, spotContext);
        if (spot == null) {
            return CompletableFuture.completedFuture(new SpotActivation(null, backendSpot, spotContext));
        }
        spotContext.setSpot(spot);
        spot.configure();
        spotContext.closeRegistration();
        spotContext.bindSubscriptions(backendSpot);
        return withCurrentOutbound(spotContext.outbound, () -> spot.onCreateAsync(List.of()))
            .thenCompose(ignored -> withCurrentOutbound(spotContext.outbound, spot::onInitializeAsync))
            .thenApply(ignored -> {
                SpotActivation activation = new SpotActivation(spot, backendSpot, spotContext);
                backendSpot.onDispatchEvent(activation::handleDispatchEvent);
                return activation;
            })
            .handle((activation, error) -> {
                if (error == null) {
                    return activation;
                }
                spotContext.closeTimers();
                backendSpot.close();
                throw new CompletionException(error);
            });
    }

    private EntrySpotActivation activateEntrySpot(
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        Class<? extends ZLinkEntrySpot> entrySpotType) {
        DefaultEntrySpotContext entryContext = new DefaultEntrySpotContext(
            nodeRid,
            backendSpot);
        ZLinkEntrySpot entrySpot = createEntrySpot(entrySpotType, entryContext);
        if (entrySpot == null) {
            backendSpot.close();
            throw new ZLinkConfigurationException(
                "entry spot requires a public constructor accepting ZLinkEntrySpotContext or a public no-arg constructor: "
                    + entrySpotType.getName());
        }
        if (entrySpot.context() != entryContext) {
            backendSpot.close();
            throw new ZLinkConfigurationException(
                "entry spot must expose the context provided by the runtime: "
                    + entrySpotType.getName());
        }
        entryContext.setEntrySpot(entrySpot);
        entrySpot.configure();
        entryContext.closeRegistration();
        entryContext.bindSubscriptions(backendSpot);
        withCurrentOutbound(entryContext.outbound, entrySpot::onInitializeAsync)
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    entryContext.closeTimers();
                    backendSpot.close();
                }
            });
        EntrySpotActivation activation = new EntrySpotActivation(
            entrySpot,
            backendSpot,
            entryContext);
        backendSpot.onDispatchEvent(activation::handleDispatchEvent);
        return activation;
    }

    private <T> CompletionStage<T> withCurrentOutbound(
        DefaultSpotOutbound outbound,
        Supplier<CompletionStage<T>> action) {
        DefaultSpotOutbound previous = currentOutbound.get();
        currentOutbound.set(outbound);
        try {
            return action.get();
        } finally {
            if (previous == null) {
                currentOutbound.remove();
            } else {
                currentOutbound.set(previous);
            }
        }
    }

    private DefaultSpotOutbound requireCurrentOutbound() {
        DefaultSpotOutbound outbound = currentOutbound.get();
        if (outbound == null) {
            throw new ZLinkConfigurationException(
                "ZLinkSpotOutbound can only be used inside an active Spot callback");
        }
        return outbound;
    }

    private CompletionStage<Void> notifySpotActorLifecycle(
        ZLinkActor actor,
        ZLinkSpotActorChangeResult result,
        List<SpotActorLifecycleHandlerRegistration> registrations) {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (SpotActorLifecycleHandlerRegistration registration : registrations) {
            if (!registration.actorType().isInstance(actor)) {
                continue;
            }
            tail = tail.thenCompose(ignored ->
                invokeSpotActorLifecycleHandler(registration, actor, result));
        }
        return tail;
    }

    private CompletionStage<Void> notifySpotActorDisconnected(ZLinkActor actor) {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (SpotActorLifecycleHandlerRegistration registration : actorDisconnectedHandlers) {
            if (!registration.actorType().isInstance(actor)) {
                continue;
            }
            tail = tail.thenCompose(ignored ->
                invokeSpotActorDisconnectedHandler(registration, actor));
        }
        return tail;
    }

    private CompletionStage<Void> invokeSpotActorDisconnectedHandler(
        SpotActorLifecycleHandlerRegistration registration,
        ZLinkActor actor) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object invocationResult =
                registration.handlerMethod().invoke(handler, actor);
            if (invocationResult instanceof CompletionStage<?> stage) {
                return stage.thenApply(ignored -> null);
            }
            return CompletableFuture.completedFuture(null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke Spot actor disconnected handler: "
                    + registration.handlerType().getName()
                    + "."
                    + registration.handlerMethod().getName()));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private ZLinkSpot spotFor(RoutingId spotRid) {
        SpotActivation activation = spots.get(spotRid);
        return activation == null ? null : activation.spot;
    }

    private CompletionStage<Void> invokeSpotActorLifecycleHandler(
        SpotActorLifecycleHandlerRegistration registration,
        ZLinkActor actor,
        ZLinkSpotActorChangeResult result) {
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object invocationResult =
                registration.handlerMethod().invoke(handler, actor, result);
            if (invocationResult instanceof CompletionStage<?> stage) {
                return stage.thenApply(ignored -> null);
            }
            return CompletableFuture.completedFuture(null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke Spot actor lifecycle handler: "
                    + registration.handlerType().getName()
                    + "."
                    + registration.handlerMethod().getName()));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private synchronized ZLinkBackendSpot publisherSpot(String channelName) {
        ZLinkBackendSpotNode node = publisherNodesByChannel.get(channelName);
        if (node == null) {
            throw new ZLinkConfigurationException(
                "SPOT publisher client is not configured: " + channelName);
        }
        return publisherSpotsByChannel.computeIfAbsent(
            channelName,
            ignored -> node.createSpot());
    }

    private void attachChannelClient(
        ZLinkChannelBackendAdapter channelAdapter,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkBackendSpotNode node,
        SpotChannelClientRegistration attached) {
        ZLinkBackendDealerSocket dealer = channelAdapter.createDealerSocket(context);
        attachedChannelDealers.add(dealer);
        if (!attached.manualConnections().isEmpty()) {
            for (String endpoint : attached.manualConnections()) {
                dealer.connect(endpoint);
            }
            node.attachChannelDealerManual(attached.channelName(), dealer);
            return;
        }

        ZLinkBackendDiscovery discovery = channelAdapter.createDiscovery(
            context,
            ZLinkBackendAutoConnectType.CLIENT_SERVER,
            attached.channelName());
        for (String endpoint : frameworkRegistration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        attachedChannelDiscoveries.add(discovery);
        dealer.attachDiscovery(discovery);
        node.attachChannelDealer(discovery, dealer);
    }

    private void attachSpotMeshDiscovery(
        ZLinkChannelBackendAdapter channelAdapter,
        ZLinkFrameworkRegistration frameworkRegistration,
        SpotNodeRegistration nodeRegistration,
        ZLinkBackendSpotNode node) {
        if (!frameworkRegistration.discoveryEnabled()
            || (!nodeRegistration.routerEnabled() && !nodeRegistration.pubSubEnabled())) {
            return;
        }
        ZLinkBackendDiscovery discovery = channelAdapter.createDiscovery(
            context,
            ZLinkBackendAutoConnectType.SPOT_MESH,
            nodeRegistration.meshName());
        for (String endpoint : frameworkRegistration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        node.attachDiscovery(discovery);
        attachedChannelDiscoveries.add(discovery);
        spotDiscoveriesByMesh.putIfAbsent(nodeRegistration.meshName(), discovery);
    }

    private void attachAcceptedSpotRouteChannel(
        ZLinkChannelBackendAdapter channelAdapter,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkBackendSpotNode node,
        SpotRouteChannelAcceptanceRegistration acceptance) {
        if (!acceptance.manualConnections().isEmpty()) {
            for (String endpoint : acceptance.manualConnections()) {
                node.connectRouterChannelPeer(acceptance.channelName(), endpoint);
            }
            return;
        }

        ZLinkBackendDiscovery discovery = channelAdapter.createDiscovery(
            context,
            acceptedRouteAutoConnectType(frameworkRegistration, acceptance.channelName()),
            acceptance.channelName());
        for (String endpoint : frameworkRegistration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        attachedChannelDiscoveries.add(discovery);
        node.attachSpotRouteChannelDiscovery(acceptance.channelName(), discovery);
    }

    private static ZLinkBackendAutoConnectType acceptedRouteAutoConnectType(
        ZLinkFrameworkRegistration frameworkRegistration,
        String channelName) {
        return frameworkRegistration.channels().stream()
            .filter(channel -> channel.name().equals(channelName))
            .findFirst()
            .map(channel -> channel.kind() == systems.zlink.framework.runtime.channels.ChannelKind.ROUTE_MESH
                ? ZLinkBackendAutoConnectType.ROUTE_MESH
                : ZLinkBackendAutoConnectType.CLIENT_SERVER)
            .orElseThrow(() -> new ZLinkConfigurationException(
                "accepted SPOT route channel is not registered: " + channelName));
    }

    private ZLinkSpot tryCreateSpot(
        Class<? extends ZLinkSpot> spotType,
        ZLinkSpotContext context) {
        try {
            return (ZLinkSpot) ZLinkHandlerFactory.services(handlerFactory)
                .add(ZLinkSpotContext.class, context)
                .create(spotType);
        } catch (RuntimeException ex) {
            if (spotType.getConstructors().length == 0) {
                return null;
            }
            throw new ZLinkConfigurationException(
                "failed to create spot: " + spotType.getName(),
                ex);
        }
    }

    private ZLinkEntrySpot createEntrySpot(
        Class<? extends ZLinkEntrySpot> entrySpotType,
        ZLinkEntrySpotContext context) {
        try {
            return (ZLinkEntrySpot) ZLinkHandlerFactory.services(handlerFactory)
                .add(ZLinkEntrySpotContext.class, context)
                .create(entrySpotType);
        } catch (RuntimeException ex) {
            if (entrySpotType.getConstructors().length == 0) {
                return null;
            }
            throw new ZLinkConfigurationException(
                "failed to create entry spot: " + entrySpotType.getName(),
                ex);
        }
    }

    private final class DefaultEntrySpotContext implements ZLinkEntrySpotContext {
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotOutbound outbound;
        private final List<DefaultSpotContext> timerContexts = new ArrayList<>();
        private final List<Class<?>> packetHandlerTypes = new ArrayList<>();
        private final List<SpotSubscriptionRegistration> subscriptionHandlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkEntrySpot entrySpot;

        DefaultEntrySpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.outbound = new DefaultSpotOutbound(nodeRid, backendSpot);
        }

        @Override
        public RoutingId spotRid() {
            return backendSpot.routingId();
        }

        @Override
        public RoutingId nodeRid() {
            return nodeRid;
        }

        void setEntrySpot(ZLinkEntrySpot entrySpot) {
            this.entrySpot = entrySpot;
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return outbound;
        }

        @Override
        public ZLinkSpotHandlerRegistry handlers() {
            return new ZLinkSpotHandlerRegistry() {
                @Override
                public void addPacket(Class<?> handlerType) {
                    ensureRegistrationOpen();
                    packetHandlerTypes.add(requireHandlerType(handlerType));
                }

                @Override
                public void addSubscribe(String topic, Class<?> handlerType) {
                    ensureRegistrationOpen();
                    subscriptionHandlerTypes.add(new SpotSubscriptionRegistration(
                        requireTopic(topic),
                        requireHandlerType(handlerType)));
                }
            };
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            DefaultSpotContext timerContext = new DefaultSpotContext(nodeRid, backendSpot);
            timerContext.setSpot(new EntrySpotTimerSurface(this));
            timerContexts.add(timerContext);
            return timerContext.addTimer(name, period, handlerType, options);
        }

        void closeTimers() {
            timerContexts.forEach(DefaultSpotContext::closeTimers);
            timerContexts.clear();
        }

        void closeRegistration() {
            registrationOpen = false;
            for (Class<?> handlerType : packetHandlerTypes) {
                SpotPacketHandlerRegistration registration =
                    createSpotPacketRegistration(handlerType, entrySpot.getClass());
                if (packetHandlers.putIfAbsent(registration.packetName(), registration) != null) {
                    throw new ZLinkConfigurationException(
                        "duplicate EntrySpot packet handler packet: " + registration.packetName());
                }
            }
            for (SpotSubscriptionRegistration subscription : subscriptionHandlerTypes) {
                SpotSubscriptionHandlerRegistration registration =
                    createSpotSubscriptionRegistration(
                        subscription.topic(),
                        subscription.handlerType(),
                        entrySpot.getClass());
                subscriptionHandlers
                    .computeIfAbsent(subscription.topic(), ignored -> new ArrayList<>())
                    .add(registration);
            }
        }

        void bindSubscriptions(ZLinkBackendSpot backendSpot) {
            for (String topic : subscriptionHandlers.keySet()) {
                backendSpot.setSubscription(topic);
            }
        }

        SpotPacketHandlerRegistration packetHandler(String packetName) {
            return packetHandlers.get(packetName);
        }

        List<SpotSubscriptionHandlerRegistration> subscriptionHandlers(String topic) {
            return subscriptionHandlers.getOrDefault(topic, List.of());
        }

        private void ensureRegistrationOpen() {
            if (!registrationOpen) {
                throw new ZLinkConfigurationException(
                    "EntrySpot handler registration is only allowed while configure is running");
            }
        }
    }

    private final class EntrySpotTimerSurface implements ZLinkSpot {
        private final DefaultEntrySpotContext context;

        EntrySpotTimerSurface(DefaultEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return new EntrySpotBackedSpotContext(context);
        }
    }

    private record EntrySpotBackedSpotContext(
        DefaultEntrySpotContext entryContext) implements ZLinkSpotContext {
        @Override
        public RoutingId spotRid() {
            return entryContext.spotRid();
        }

        @Override
        public RoutingId nodeRid() {
            return entryContext.nodeRid();
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return entryContext.outbound();
        }

        @Override
        public CompletionStage<Void> leaveActorAsync(
            systems.zlink.framework.actors.ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            return entryContext.addTimer(name, period, handlerType, options);
        }
    }

    private final class EntrySpotActivation implements AutoCloseable {
        private final ZLinkEntrySpot entrySpot;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultEntrySpotContext context;

        EntrySpotActivation(
            ZLinkEntrySpot entrySpot,
            ZLinkBackendSpot backendSpot,
            DefaultEntrySpotContext context) {
            this.entrySpot = entrySpot;
            this.backendSpot = backendSpot;
            this.context = context;
        }

        void handleDispatchEvent(ZLinkBackendSpotDispatchInfo info) {
            if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                drainRoutes();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE) {
                drainSubscriptions();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
                drainUnhandledActorJoins();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                dispatchActorMessages(info.actorMessages());
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE) {
                drainActorLifecycleEvents();
            }
            for (ZLinkBackendActorReceived actorMessage : info.actorMessages()) {
                actorMessage.close();
            }
        }

        private void drainRoutes() {
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchRoute(received);
            }
        }

        private void dispatchRoute(ZLinkBackendReceived received) {
            if (received.parts().isEmpty()) {
                received.close();
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            SpotPacketHandlerRegistration handler =
                context.packetHandler(packet.packetName());
            if (handler == null) {
                received.close();
                return;
            }
            if (received.requestSeq().isPresent()) {
                if (!handler.request()) {
                    received.close();
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotRequestHandler(handler, entrySpot, payloadCopy)
                    .thenAccept(reply -> received.reply(List.of(reply)))
                    .whenComplete((ignored, error) -> {
                        payloadCopy.close();
                        received.close();
                    });
                return;
            }
            try (received) {
                if (handler.request()) {
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotPacketHandler(handler, entrySpot, payloadCopy)
                    .whenComplete((ignored, error) -> payloadCopy.close());
            }
        }

        private void drainSubscriptions() {
            while (true) {
                ZLinkBackendTopicMessage received =
                    backendSpot.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchSubscription(received);
            }
        }

        private void dispatchSubscription(ZLinkBackendTopicMessage received) {
            try {
                if (received.parts().isEmpty()) {
                    return;
                }
                ParsedPacket packet = parsePacket(received.parts());
                for (SpotSubscriptionHandlerRegistration handler :
                    context.subscriptionHandlers(received.topic())) {
                    if (!handler.packetName().equals(packet.packetName())) {
                        continue;
                    }
                    Message payloadCopy = Message.from(packet.payload());
                    invokeSpotSubscriptionHandler(handler, entrySpot, payloadCopy)
                        .whenComplete((ignored, error) -> payloadCopy.close());
                }
            } finally {
                received.parts().forEach(Message::close);
            }
        }

        private void drainActorLifecycleEvents() {
            while (true) {
                ZLinkBackendActorLifecycleEvent event =
                    backendSpot.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
                if (event == null) {
                    return;
                }
                if (actorRuntime == null) {
                    continue;
                }
                ZLinkBackendActorRef actorRef = event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
                    ? event.info().previousActor()
                    : event.info().currentActor();
                actorRuntime.localActor(actorRef.actorId())
                    .ifPresent(actor -> dispatchActorLifecycle(event, actorRef, actor));
            }
        }

        private void dispatchActorLifecycle(
            ZLinkBackendActorLifecycleEvent event,
            ZLinkBackendActorRef actorRef,
            ZLinkActor actor) {
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
                actorRuntime.markLeft(actor);
                ZLinkSpotActorChangeResult result =
                    new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.LEAVE_SPOT);
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> notifySpotActorLifecycle(actor, result, actorLeftHandlers));
                return;
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            actorRuntime.markJoined(actor, actorRef, spotRid, spotFor(spotRid));
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            actorRuntime.submitActorDispatch(
                actor.actorId(),
                () -> notifySpotActorLifecycle(actor, result, actorJoinedHandlers));
        }

        private void dispatchActorMessages(List<ZLinkBackendActorReceived> actorMessages) {
            int index = 0;
            while (index + 1 < actorMessages.size()) {
                ZLinkBackendActorReceived headerPart = actorMessages.get(index++);
                ZLinkBackendActorReceived bodyPart = actorMessages.get(index++);
                ActorPacketHeader packetHeader = decodeActorPacketHeader(headerPart);
                SpotActorPacketHandlerRegistration handler =
                    actorPacketHandlers.get(packetHeader.packetName());
                if (handler == null || actorRuntime == null) {
                    continue;
                }
                if (packetHeader.requestSeq().isPresent()
                    != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                    continue;
                }
                actorRuntime.localActor(headerPart.actor().actorId())
                    .filter(actor -> handler.actorType().isInstance(actor))
                    .ifPresent(actor -> {
                        Message payloadCopy = Message.from(bodyPart.message());
                        actorRuntime.submitActorDispatch(
                            actor.actorId(),
                            () -> dispatchActorPacket(
                                handler,
                                actor,
                                packetHeader,
                                headerPart,
                                payloadCopy));
                    });
            }
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            ZLinkActor actor,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            CompletionStage<Optional<Message>> stage = handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                ? invokeActorRequestHandler(handler, actor, payload)
                : invokeActorSendHandler(handler, actor, payload).thenApply(ignored -> Optional.empty());
            return stage.thenAccept(reply -> {
                    if (reply.isEmpty()) {
                        return;
                    }
                    try (Message frame = encodeActorReplyFrame(packetHeader, headerPart, reply.get())) {
                        if (!primaryNode.sendActorBoundSession(
                            new ZLinkBackendActorRef(
                                headerPart.actor().nodeRid(),
                                actor.actorId(),
                                headerPart.actor().epoch()),
                            List.of(frame),
                            SendFlags.NONE)) {
                            throw new ZLinkConfigurationException(
                                "actor bound session reply failed: " + actor.actorId());
                        }
                    }
                })
                .whenComplete((ignored, error) -> payload.close());
        }

        private ActorPacketHeader decodeActorPacketHeader(ZLinkBackendActorReceived headerPart) {
            byte[] bytes = headerPart.message().toByteArray();
            try {
                return decodeStreamActorPacketHeader(bytes, headerPart.requestSeq());
            } catch (RuntimeException ignored) {
                return new ActorPacketHeader(
                    headerPart.message().toUtf8String(),
                    headerPart.requestSeq(),
                    false,
                    0);
            }
        }

        private ActorPacketHeader decodeStreamActorPacketHeader(
            byte[] bytes,
            Optional<Long> backendRequestSeq) {
            if (bytes.length < 4) {
                throw new IllegalArgumentException("actor STREAM header is too short");
            }
            ByteBuffer buffer = ByteBuffer.wrap(bytes);
            int kind = Byte.toUnsignedInt(buffer.get());
            int codec = Byte.toUnsignedInt(buffer.get());
            int flags = Byte.toUnsignedInt(buffer.get());
            Optional<Long> requestSeq = backendRequestSeq;
            if ((flags & 0x01) != 0) {
                if (buffer.remaining() < Long.BYTES) {
                    throw new IllegalArgumentException("actor STREAM header missing request sequence");
                }
                long decodedRequestSeq = buffer.getLong();
                if (decodedRequestSeq == 0) {
                    throw new IllegalArgumentException("actor STREAM request sequence is zero");
                }
                requestSeq = Optional.of(decodedRequestSeq);
            }
            if (buffer.remaining() < 1) {
                throw new IllegalArgumentException("actor STREAM header missing packet name");
            }
            int nameLength = Byte.toUnsignedInt(buffer.get());
            if (buffer.remaining() < nameLength) {
                throw new IllegalArgumentException("actor STREAM header packet name is truncated");
            }
            byte[] nameBytes = new byte[nameLength];
            buffer.get(nameBytes);
            if ((flags & 0x02) != 0) {
                if (buffer.remaining() < 2) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                int metadataLength = Short.toUnsignedInt(buffer.getShort());
                if (buffer.remaining() < metadataLength) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                buffer.position(buffer.position() + metadataLength);
            }
            if (buffer.hasRemaining()) {
                throw new IllegalArgumentException("actor STREAM header has trailing bytes");
            }
            if (kind != 1 && kind != 2) {
                throw new IllegalArgumentException("actor STREAM header is not dispatch kind");
            }
            return new ActorPacketHeader(
                new String(nameBytes, StandardCharsets.UTF_8),
                requestSeq,
                true,
                codec);
        }

        private Message encodeActorReplyFrame(
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived originalHeader,
            Message payload) {
            if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
                return Message.from(payload);
            }
            byte[] name = packetHeader.packetName().getBytes(StandardCharsets.UTF_8);
            ByteBuffer header = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
            header.put((byte) 3);
            header.put((byte) packetHeader.codec());
            header.put((byte) 0x01);
            header.putLong(packetHeader.requestSeq().get());
            header.put((byte) name.length);
            header.put(name);
            byte[] headerBytes = header.array();
            byte[] body = payload.toByteArray();
            ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
            frame.putShort((short) headerBytes.length);
            frame.putInt(body.length);
            frame.put(headerBytes);
            frame.put(body);
            return Message.from(frame.array());
        }

        private record ActorPacketHeader(
            String packetName,
            Optional<Long> requestSeq,
            boolean streamHeader,
            int codec) {
        }

        private void drainUnhandledActorJoins() {
            while (true) {
                ZLinkBackendActorJoinRequest request =
                    backendSpot.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
                if (request == null) {
                    return;
                }
                try {
                    SpotActorJoinHandlerRegistration handler =
                        resolveActorJoinHandler(request);
                    if (handler == null) {
                        try (Message emptyReply = Message.from(new byte[0])) {
                            backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                        }
                        continue;
                    }
                    Message payload = actorJoinPayload(request);
                    invokeActorJoinHandler(handler, request, payload)
                        .whenComplete((reply, error) -> {
                            if (error != null) {
                                try (Message emptyReply = Message.from(new byte[0])) {
                                    backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                                }
                                return;
                            }
                            backendSpot.replyActorJoin(request, 0, List.of(reply));
                            reply.close();
                        });
                } finally {
                    request.parts().forEach(Message::close);
                }
            }
        }

        private SpotActorJoinHandlerRegistration resolveActorJoinHandler(
            ZLinkBackendActorJoinRequest request) {
            if (request.parts().isEmpty()) {
                return null;
            }
            if (request.parts().size() >= 2) {
                return actorJoinHandlers.get(request.parts().get(0).toUtf8String());
            }
            for (SpotActorJoinHandlerRegistration handler : actorJoinHandlers.values()) {
                if (handler.requestType() == Message.class
                    || handler.requestType() == byte[].class
                    || handler.requestType() == String.class) {
                    return handler;
                }
            }
            return null;
        }

        private Message actorJoinPayload(ZLinkBackendActorJoinRequest request) {
            return request.parts().size() >= 2 ? request.parts().get(1) : request.parts().get(0);
        }

        private CompletionStage<Message> invokeActorJoinHandler(
            SpotActorJoinHandlerRegistration registration,
            ZLinkBackendActorJoinRequest request,
            Message payload) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor join handler"));
            }
            return actorRuntime
                .getOrCreateLocalActor(
                    request.targetActor().actorId(),
                    registration.actorType())
                .thenCompose(actor -> {
                    if (actor.isEmpty()) {
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "Spot actor join target actor is not available: "
                                + request.targetActor().actorId()));
                    }
                    actorRuntime.markJoined(
                        actor.get(),
                        request.targetActor(),
                        backendSpot.routingId(),
                        spotFor(backendSpot.routingId()));
                    try {
                        Object handler = handlerFactory.create(registration.handlerType());
                        Object requestObject =
                            serializer.deserialize(payload, registration.requestType());
                        Object result = registration.handlerMethod().invoke(
                            handler,
                            actor.get(),
                            requestObject);
                        CompletionStage<?> stage =
                            result instanceof CompletionStage<?> completionStage
                                ? completionStage
                                : CompletableFuture.completedFuture(result);
                        return stage
                            .thenCompose(reply -> notifyActorJoined(actor.get())
                                .thenApply(ignored -> reply))
                            .whenComplete((reply, error) -> {
                                if (error != null) {
                                    actorRuntime.markLeft(actor.get());
                                }
                            })
                            .thenApply(serializer::serialize);
                    } catch (IllegalAccessException | InvocationTargetException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "failed to invoke Spot actor join handler: "
                                + registration.handlerType().getName()
                                + "."
                                + registration.handlerMethod().getName(),
                            ex instanceof InvocationTargetException invocation
                                ? invocation.getCause()
                                : ex));
                    } catch (RuntimeException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(ex);
                    }
                });
        }

        private CompletionStage<Void> notifyActorJoined(ZLinkActor actor) {
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            return notifyActorLifecycle(actor, result, actorJoinedHandlers);
        }

        private CompletionStage<Void> notifyActorLifecycle(
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result,
            List<SpotActorLifecycleHandlerRegistration> registrations) {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            for (SpotActorLifecycleHandlerRegistration registration : registrations) {
                if (!registration.actorType().isInstance(actor)) {
                    continue;
                }
                tail = tail.thenCompose(ignored ->
                    invokeActorJoinedHandler(registration, actor, result));
            }
            return tail;
        }

        private CompletionStage<Void> invokeActorJoinedHandler(
            SpotActorLifecycleHandlerRegistration registration,
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object invocationResult =
                    registration.handlerMethod().invoke(handler, actor, result);
                if (invocationResult instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor joined handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Void> invokeActorSendHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(handler, actor, message);
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor send handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Optional<Message>> invokeActorRequestHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(handler, actor, message);
                CompletionStage<?> stage = result instanceof CompletionStage<?> completionStage
                    ? completionStage
                    : CompletableFuture.completedFuture(result);
                return stage.thenApply(reply -> Optional.of(serializer.serialize(reply)));
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor request handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        @Override
        public void close() {
            entrySpot.onClosingAsync()
                .whenComplete((ignored, error) -> {
                    context.closeTimers();
                    backendSpot.close();
                });
        }
    }

    private final class DefaultSpotContext implements ZLinkSpotContext {
        private static final CancellationToken NONE = () -> false;
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotOutbound outbound;
        private final List<ManagedTimer> timers = new ArrayList<>();
        private final List<Class<?>> packetHandlerTypes = new ArrayList<>();
        private final List<SpotSubscriptionRegistration> subscriptionHandlerTypes = new ArrayList<>();
        private final Map<String, SpotPacketHandlerRegistration> packetHandlers = new HashMap<>();
        private final Map<String, List<SpotSubscriptionHandlerRegistration>> subscriptionHandlers =
            new HashMap<>();
        private boolean registrationOpen = true;
        private ZLinkSpot spot;

        DefaultSpotContext(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
            this.outbound = new DefaultSpotOutbound(nodeRid, backendSpot);
        }

        void setSpot(ZLinkSpot spot) {
            this.spot = spot;
        }

        @Override
        public RoutingId spotRid() {
            return backendSpot.routingId();
        }

        @Override
        public RoutingId nodeRid() {
            return nodeRid;
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return outbound;
        }

        @Override
        public ZLinkSpotHandlerRegistry handlers() {
            return new ZLinkSpotHandlerRegistry() {
                @Override
                public void addPacket(Class<?> handlerType) {
                    ensureRegistrationOpen();
                    packetHandlerTypes.add(requireHandlerType(handlerType));
                }

                @Override
                public void addSubscribe(String topic, Class<?> handlerType) {
                    ensureRegistrationOpen();
                    subscriptionHandlerTypes.add(new SpotSubscriptionRegistration(
                        requireTopic(topic),
                        requireHandlerType(handlerType)));
                }
            };
        }

        @Override
        public CompletionStage<Void> leaveActorAsync(systems.zlink.framework.actors.ZLinkActor actor) {
            if (actor == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor is required"));
            }
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor leave"));
            }
            actorRuntime.markLeft(actor);
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.LEAVE_SPOT);
            return notifySpotActorLifecycle(actor, result, actorLeftHandlers);
        }

        @Override
        public CompletionStage<ZLinkTimer> addTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            if (name == null || name.isBlank()) {
                throw new ZLinkConfigurationException("timer name is required");
            }
            if (period == null || period.isNegative() || period.isZero()) {
                throw new ZLinkConfigurationException("timer period must be positive");
            }
            ManagedTimer timer = new ManagedTimer(name, period, handlerType);
            timers.add(timer);
            timer.start();
            return CompletableFuture.completedFuture(timer);
        }

        void closeTimers() {
            timers.forEach(ManagedTimer::close);
            timers.clear();
        }

        void closeRegistration() {
            registrationOpen = false;
            for (Class<?> handlerType : packetHandlerTypes) {
                SpotPacketHandlerRegistration registration =
                    createSpotPacketRegistration(handlerType, spot.getClass());
                if (packetHandlers.putIfAbsent(registration.packetName(), registration) != null) {
                    throw new ZLinkConfigurationException(
                        "duplicate SPOT packet handler packet: " + registration.packetName());
                }
            }
            for (SpotSubscriptionRegistration subscription : subscriptionHandlerTypes) {
                SpotSubscriptionHandlerRegistration registration =
                    createSpotSubscriptionRegistration(
                        subscription.topic(),
                        subscription.handlerType(),
                        spot.getClass());
                subscriptionHandlers
                    .computeIfAbsent(subscription.topic(), ignored -> new ArrayList<>())
                    .add(registration);
            }
        }

        void bindSubscriptions(ZLinkBackendSpot backendSpot) {
            for (String topic : subscriptionHandlers.keySet()) {
                backendSpot.setSubscription(topic);
            }
        }

        SpotPacketHandlerRegistration packetHandler(String packetName) {
            return packetHandlers.get(packetName);
        }

        List<SpotSubscriptionHandlerRegistration> subscriptionHandlers(String topic) {
            return subscriptionHandlers.getOrDefault(topic, List.of());
        }

        private void ensureRegistrationOpen() {
            if (!registrationOpen) {
                throw new ZLinkConfigurationException(
                    "SPOT handler registration is only allowed while configure is running");
            }
        }

        private final class ManagedTimer implements ZLinkTimer {
            private final String name;
            private final Duration period;
            private final Class<?> handlerType;
            private final ZLinkAsyncSerialQueue dispatchQueue = new ZLinkAsyncSerialQueue();
            private final Instant startedAt = Instant.now();
            private long tickIndex;
            private volatile boolean disposed;
            private ScheduledFuture<?> future;

            ManagedTimer(String name, Duration period, Class<?> handlerType) {
                this.name = name;
                this.period = period;
                this.handlerType = handlerType;
            }

            void start() {
                future = timerExecutor.scheduleAtFixedRate(
                    this::dispatch,
                    period.toNanos(),
                    period.toNanos(),
                    TimeUnit.NANOSECONDS);
            }

            private void dispatch() {
                if (disposed) {
                    return;
                }
                long index = ++tickIndex;
                Instant now = Instant.now();
                ZLinkTimerTick tick = new ZLinkTimerTick(
                    name,
                    index,
                    index,
                    period,
                    startedAt.plusNanos(period.toNanos() * index),
                    now,
                    Duration.ofNanos(period.toNanos() * index),
                    Duration.between(startedAt, now),
                    Duration.between(startedAt.plusNanos(period.toNanos() * index), now),
                    0);
                dispatchQueue.enqueue(() ->
                    withCurrentOutbound(DefaultSpotContext.this.outbound, () ->
                        invokeTimerHandler(handlerType, spot, tick)));
            }

            @Override
            public boolean isDisposed() {
                return disposed;
            }

            @Override
            public CompletionStage<Void> cancelAsync() {
                close();
                return CompletableFuture.completedFuture(null);
            }

            @Override
            public void close() {
                disposed = true;
                if (future != null) {
                    future.cancel(false);
                }
            }
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeTimerHandler(Class<?> handlerType, ZLinkSpot spot, ZLinkTimerTick tick) {
        try {
            Object handler = handlerFactory.create(handlerType);
            if (handler instanceof ZLinkSpotTimerHandler timerHandler) {
                return timerHandler.handleAsync(spot, tick);
            }
            return CompletableFuture.completedFuture(null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to create timer handler: " + handlerType.getName(),
                ex));
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeSpotPacketHandler(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            if (registration.handlerMethod() != null) {
                Object result = registration.handlerMethod().invoke(handler, spot, message);
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            }
            return ((ZLinkSpotPacketHandler) handler)
                .handleAsync(spot, message)
                .thenApply(ignored -> null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT packet handler: "
                    + registration.handlerType().getName(),
                ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Message> invokeSpotRequestHandler(
        SpotPacketHandlerRegistration registration,
        Object spot,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            Object result = registration.handlerMethod() != null
                ? registration.handlerMethod().invoke(handler, spot, message)
                : ((ZLinkSpotRequestHandler) handler).handleAsync(spot, message);
            CompletionStage<?> stage = result instanceof CompletionStage<?> completion
                ? completion
                : CompletableFuture.completedFuture(result);
            return stage.thenApply(serializer::serialize);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT request handler: "
                    + registration.handlerType().getName(),
                ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeSpotSubscriptionHandler(
        SpotSubscriptionHandlerRegistration registration,
        Object spot,
        Message payload) {
        Object message = serializer.deserialize(payload, registration.messageType());
        try {
            Object handler = handlerFactory.create(registration.handlerType());
            if (registration.handlerMethod() != null) {
                Object result = registration.handlerMethod().invoke(handler, spot, message);
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            }
            return ((ZLinkSpotSubscriptionHandler) handler)
                .handleAsync(spot, message)
                .thenApply(ignored -> null);
        } catch (IllegalAccessException | InvocationTargetException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to invoke SPOT subscription handler: "
                    + registration.handlerType().getName(),
                ex));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private final class DefaultSpotOutbound implements ZLinkSpotOutbound {
        private final RoutingId nodeRid;
        private final ZLinkBackendSpot backendSpot;

        DefaultSpotOutbound(RoutingId nodeRid, ZLinkBackendSpot backendSpot) {
            this.nodeRid = nodeRid;
            this.backendSpot = backendSpot;
        }

        @Override
        public <TMessage> ZLinkSendCall sendToSpot(RoutingId spotRid, TMessage message) {
            requireRoutingId(spotRid);
            if (channels != null && !egressChannels.isEmpty()) {
                if (egressChannels.size() > 1) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT outbound requires a single egress channel until remote address resolution is implemented");
                }
                return new EgressSpotSendCall(
                    channels,
                    egressChannels.get(0),
                    spotRid,
                    serializer.serialize(message),
                    Optional.empty());
            }
            return new SpotToSpotSendCall(
                backendSpot,
                nodeRid,
                spotRid,
                serializer.serialize(message),
                Optional.empty());
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToSpot(RoutingId spotRid, TMessage request) {
            requireRoutingId(spotRid);
            if (channels != null && !egressChannels.isEmpty()) {
                if (egressChannels.size() > 1) {
                    throw new ZLinkConfigurationException(
                        "routed SPOT outbound requires a single egress channel until remote address resolution is implemented");
                }
                return new EgressSpotRequestCall(
                    channels,
                    egressChannels.get(0),
                    spotRid,
                    serializer.serialize(request),
                    Optional.empty(),
                    defaultTimeout);
            }
            return new SpotToSpotRequestCall(
                backendSpot,
                nodeRid,
                spotRid,
                serializer.serialize(request),
                Optional.empty(),
                defaultTimeout);
        }

        @Override
        public <TEvent> ZLinkPublishCall publish(String topic, TEvent message) {
            return new SpotPublishCall(backendSpot, topic, serializer.serialize(message), Optional.empty());
        }

        @Override
        public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
            return new SpotChannelSendCall(backendSpot, channelName, serializer.serialize(message), Optional.empty());
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage request) {
            return new SpotChannelRequestCall(
                backendSpot,
                channelName,
                serializer.serialize(request),
                Optional.empty(),
                defaultTimeout);
        }
    }

    private record EgressSpotSendCall(
        ZLinkChannelRuntime channels,
        String egressChannelName,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new EgressSpotSendCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            List<Message> spotParts = parts(packetName, payload);
            try {
                return channels.sendToSpotViaEgressChannel(
                    egressChannelName,
                    spotRid,
                    spotParts);
            } finally {
                spotParts.forEach(Message::close);
            }
        }
    }

    private final class EgressSpotRequestCall implements ZLinkRequestCall {
        private final ZLinkChannelRuntime channels;
        private final String egressChannelName;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        EgressSpotRequestCall(
            ZLinkChannelRuntime channels,
            String egressChannelName,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.channels = channels;
            this.egressChannelName = egressChannelName;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new EgressSpotRequestCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                Optional.of(packetName),
                timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new EgressSpotRequestCall(
                channels,
                egressChannelName,
                spotRid,
                payload,
                packetName,
                timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            List<Message> spotParts = parts(packetName, payload);
            try {
                return channels.requestToSpotViaEgressChannel(
                    egressChannelName,
                    spotRid,
                    spotParts,
                    timeout)
                    .thenApply(replyParts -> {
                        Message emptyReply = null;
                        try {
                            Message firstReply = replyParts.isEmpty()
                                ? (emptyReply = Message.from(new byte[0]))
                                : replyParts.get(0);
                            return serializer.deserialize(firstReply, replyType);
                        } finally {
                            if (emptyReply != null) {
                                emptyReply.close();
                            }
                            replyParts.forEach(Message::close);
                        }
                    });
            } finally {
                spotParts.forEach(Message::close);
            }
        }
    }

    private final class AmbientSpotOutbound implements ZLinkSpotOutbound {
        @Override
        public <TMessage> ZLinkSendCall sendToSpot(RoutingId spotRid, TMessage message) {
            return requireCurrentOutbound().sendToSpot(spotRid, message);
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToSpot(
            RoutingId spotRid,
            TMessage request) {
            return requireCurrentOutbound().requestToSpot(spotRid, request);
        }

        @Override
        public <TEvent> ZLinkPublishCall publish(String topic, TEvent message) {
            return requireCurrentOutbound().publish(topic, message);
        }

        @Override
        public <TMessage> ZLinkSendCall sendToChannel(
            String channelName,
            TMessage message) {
            return requireCurrentOutbound().sendToChannel(channelName, message);
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToChannel(
            String channelName,
            TMessage request) {
            return requireCurrentOutbound().requestToChannel(channelName, request);
        }
    }

    private final class DefaultSpotPublisherClient implements ZLinkSpotPublisherClient {
        @Override
        public <TEvent> ZLinkPublishCall publishSpot(
            String channelName,
            String topic,
            TEvent message) {
            if (channelName == null || channelName.isBlank()) {
                throw new ZLinkConfigurationException("SPOT publisher channel name is required");
            }
            if (topic == null || topic.isBlank()) {
                throw new ZLinkConfigurationException("SPOT publish topic is required");
            }
            if (!publisherNodesByChannel.containsKey(channelName)) {
                throw new ZLinkConfigurationException(
                    "SPOT publisher client is not configured: " + channelName);
            }
            return new ExternalSpotPublishCall(
                channelName,
                topic,
                serializer.serialize(message),
                Optional.empty());
        }
    }

    private final class ExternalSpotPublishCall implements ZLinkPublishCall {
        private final String channelName;
        private final String topic;
        private final Message payload;
        private final Optional<String> packetName;

        private ExternalSpotPublishCall(
            String channelName,
            String topic,
            Message payload,
            Optional<String> packetName) {
            this.channelName = channelName;
            this.topic = topic;
            this.payload = payload;
            this.packetName = packetName;
        }

        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new ExternalSpotPublishCall(
                channelName,
                topic,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkPublishCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    publisherSpot(channelName).publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private String resolveRouterChannelId(String configuredRouterChannelId) {
        if (configuredRouterChannelId != null && !configuredRouterChannelId.isBlank()) {
            return configuredRouterChannelId;
        }
        if (routeMeshChannels.size() == 1) {
            return routeMeshChannels.get(0);
        }
        throw new ZLinkConfigurationException(
            "registry SPOT remote addresses require a single route mesh egress channel or an explicit routerChannelId");
    }

    private ZLinkBackendDiscovery resolveSpotDiscovery(String namespaceName) {
        if (namespaceName != null) {
            ZLinkBackendDiscovery discovery = spotDiscoveriesByMesh.get(namespaceName);
            if (discovery != null) {
                return discovery;
            }
        }
        if (spotDiscoveriesByMesh.size() == 1) {
            return spotDiscoveriesByMesh.values().iterator().next();
        }
        throw new ZLinkConfigurationException(
            "registry SPOT remote addresses require a unique Spot mesh discovery");
    }

    private record SpotToSpotSendCall(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SpotToSpotSendCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.sendToSpot(targetNodeRid, spotRid, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private final class SpotToSpotRequestCall implements ZLinkRequestCall {
        private final ZLinkBackendSpot spot;
        private final RoutingId targetNodeRid;
        private final RoutingId spotRid;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private SpotToSpotRequestCall(
            ZLinkBackendSpot spot,
            RoutingId targetNodeRid,
            RoutingId spotRid,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.spot = spot;
            this.targetNodeRid = targetNodeRid;
            this.spotRid = spotRid;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new SpotToSpotRequestCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                Optional.of(packetName),
                timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new SpotToSpotRequestCall(
                spot,
                targetNodeRid,
                spotRid,
                payload,
                packetName,
                timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            List<Message> requestParts = parts(packetName, payload);
            try {
                spot.requestToSpot(targetNodeRid, spotRid, requestParts, reply -> {
                    Message emptyReply = null;
                    try {
                        Message firstReply = reply.parts().isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : reply.parts().get(0);
                        result.complete(serializer.deserialize(firstReply, replyType));
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        if (emptyReply != null) {
                            emptyReply.close();
                        }
                        reply.parts().forEach(Message::close);
                    }
                }, SendFlags.NONE, timeout);
            } catch (RuntimeException ex) {
                result.completeExceptionally(ex);
            } finally {
                requestParts.forEach(Message::close);
            }
            return result;
        }
    }

    private record SpotChannelSendCall(
        ZLinkBackendSpot spot,
        String channelName,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SpotChannelSendCall(spot, channelName, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.sendToChannel(channelName, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private final class SpotChannelRequestCall implements ZLinkRequestCall {
        private final ZLinkBackendSpot spot;
        private final String channelName;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        private SpotChannelRequestCall(
            ZLinkBackendSpot spot,
            String channelName,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.spot = spot;
            this.channelName = channelName;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new SpotChannelRequestCall(spot, channelName, payload, Optional.of(packetName), timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new SpotChannelRequestCall(spot, channelName, payload, packetName, timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            List<Message> requestParts = parts(packetName, payload);
            try {
                spot.requestToChannel(channelName, requestParts, reply -> {
                    Message emptyReply = null;
                    try {
                        Message firstReply = reply.parts().isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : reply.parts().get(0);
                        result.complete(serializer.deserialize(firstReply, replyType));
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        if (emptyReply != null) {
                            emptyReply.close();
                        }
                        reply.parts().forEach(Message::close);
                    }
                }, SendFlags.NONE, timeout);
            } catch (RuntimeException ex) {
                result.completeExceptionally(ex);
            } finally {
                requestParts.forEach(Message::close);
            }
            return result;
        }
    }

    private record SpotPublishCall(
        ZLinkBackendSpot spot,
        String topic,
        Message payload,
        Optional<String> packetName) implements ZLinkPublishCall {
        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new SpotPublishCall(spot, topic, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkPublishCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> parts = parts(packetName, payload);
                try {
                    spot.publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private static List<Message> parts(Optional<String> packetName, Message payload) {
        return packetName
            .map(name -> List.of(Message.from(name.getBytes(StandardCharsets.UTF_8)), payload))
            .orElseGet(() -> List.of(payload));
    }

    private static ParsedPacket parsePacket(List<Message> parts) {
        if (parts.size() >= 2) {
            return new ParsedPacket(parts.get(0).toUtf8String(), parts.get(1));
        }
        return new ParsedPacket("", parts.get(0));
    }

    private record ParsedPacket(String packetName, Message payload) {
    }

    private record SpotSubscriptionRegistration(String topic, Class<?> handlerType) {
    }

    private record SpotPacketHandlerRegistration(
        Class<?> handlerType,
        Method handlerMethod,
        Class<?> spotType,
        Class<?> messageType,
        Class<?> replyType,
        String packetName,
        boolean request) {
    }

    private record SpotSubscriptionHandlerRegistration(
        String topic,
        Class<?> handlerType,
        Method handlerMethod,
        Class<?> spotType,
        Class<?> messageType,
        String packetName) {
    }

    private static SpotPacketHandlerRegistration createSpotPacketRegistration(
        Class<?> handlerType,
        Class<?> expectedSpotType) {
        ParameterizedType packet = findInterface(handlerType, ZLinkSpotPacketHandler.class);
        if (packet != null) {
            Type[] arguments = packet.getActualTypeArguments();
            Class<?> spotType = requireClassArgument(handlerType, arguments[0]);
            requireExactSpotType(handlerType, expectedSpotType, spotType);
            Class<?> messageType = requireClassArgument(handlerType, arguments[1]);
            return new SpotPacketHandlerRegistration(
                handlerType, null, spotType, messageType, Void.class,
                resolvePacketName(messageType), false);
        }

        ParameterizedType request = findInterface(handlerType, ZLinkSpotRequestHandler.class);
        if (request != null) {
            Type[] arguments = request.getActualTypeArguments();
            Class<?> spotType = requireClassArgument(handlerType, arguments[0]);
            requireExactSpotType(handlerType, expectedSpotType, spotType);
            Class<?> messageType = requireClassArgument(handlerType, arguments[1]);
            Class<?> replyType = requireClassArgument(handlerType, arguments[2]);
            return new SpotPacketHandlerRegistration(
                handlerType, null, spotType, messageType, replyType,
                resolvePacketName(messageType), true);
        }

        for (Method method : handlerType.getMethods()) {
            ZLinkSpotRequest annotation = method.getAnnotation(ZLinkSpotRequest.class);
            if (annotation == null) {
                continue;
            }
            Class<?>[] parameters = method.getParameterTypes();
            if (parameters.length != 2) {
                throw new ZLinkConfigurationException(
                    "SPOT request handler method must have spot and request parameters: "
                        + handlerType.getName() + "." + method.getName());
            }
            requireExactSpotType(handlerType, expectedSpotType, parameters[0]);
            return new SpotPacketHandlerRegistration(
                handlerType, method, parameters[0], parameters[1],
                resolveReplyType(handlerType, method),
                resolvePacketName(parameters[1], annotation.packetName()), true);
        }

        throw new ZLinkConfigurationException(
            "SPOT packet handler must implement ZLinkSpotPacketHandler or ZLinkSpotRequestHandler: "
                + handlerType.getName());
    }

    private static SpotSubscriptionHandlerRegistration createSpotSubscriptionRegistration(
        String topic,
        Class<?> handlerType,
        Class<?> expectedSpotType) {
        ParameterizedType subscription = findInterface(handlerType, ZLinkSpotSubscriptionHandler.class);
        if (subscription != null) {
            Type[] arguments = subscription.getActualTypeArguments();
            Class<?> spotType = requireClassArgument(handlerType, arguments[0]);
            requireExactSpotType(handlerType, expectedSpotType, spotType);
            Class<?> messageType = requireClassArgument(handlerType, arguments[1]);
            return new SpotSubscriptionHandlerRegistration(
                topic, handlerType, null, spotType, messageType, resolvePacketName(messageType));
        }

        for (Method method : handlerType.getMethods()) {
            ZLinkSpotSubscription annotation = method.getAnnotation(ZLinkSpotSubscription.class);
            if (annotation == null) {
                continue;
            }
            Class<?>[] parameters = method.getParameterTypes();
            if (parameters.length != 2) {
                throw new ZLinkConfigurationException(
                    "SPOT subscription handler method must have spot and event parameters: "
                        + handlerType.getName() + "." + method.getName());
            }
            requireExactSpotType(handlerType, expectedSpotType, parameters[0]);
            return new SpotSubscriptionHandlerRegistration(
                topic, handlerType, method, parameters[0], parameters[1],
                resolvePacketName(parameters[1]));
        }

        throw new ZLinkConfigurationException(
            "SPOT subscription handler must implement ZLinkSpotSubscriptionHandler: "
                + handlerType.getName());
    }

    private static void requireExactSpotType(
        Class<?> handlerType,
        Class<?> expectedSpotType,
        Class<?> actualSpotType) {
        if (actualSpotType != expectedSpotType) {
            throw new ZLinkConfigurationException(
                "SPOT handler " + handlerType.getName()
                    + " targets " + actualSpotType.getName()
                    + " but expected " + expectedSpotType.getName());
        }
    }

    private static ParameterizedType findInterface(Class<?> type, Class<?> targetRawType) {
        for (Type interfaceType : type.getGenericInterfaces()) {
            ParameterizedType matched = matchInterface(interfaceType, targetRawType);
            if (matched != null) {
                return matched;
            }
        }
        Class<?> superclass = type.getSuperclass();
        return superclass == null || superclass == Object.class
            ? null
            : findInterface(superclass, targetRawType);
    }

    private static ParameterizedType matchInterface(Type interfaceType, Class<?> targetRawType) {
        if (interfaceType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == targetRawType) {
            return parameterized;
        }
        if (interfaceType instanceof Class<?> raw) {
            return findInterface(raw, targetRawType);
        }
        return null;
    }

    private static Class<?> requireClassArgument(Class<?> handlerType, Type argument) {
        if (argument instanceof Class<?> klass) {
            return klass;
        }
        if (argument instanceof ParameterizedType parameterized
            && parameterized.getRawType() instanceof Class<?> raw) {
            return raw;
        }
        throw new ZLinkConfigurationException(
            "handler generic argument must resolve to a class: " + handlerType.getName());
    }

    private static Class<?> resolveReplyType(Class<?> handlerType, Method method) {
        Type returnType = method.getGenericReturnType();
        if (returnType instanceof ParameterizedType parameterized
            && parameterized.getRawType() == CompletionStage.class) {
            return requireClassArgument(handlerType, parameterized.getActualTypeArguments()[0]);
        }
        if (method.getReturnType() == Void.TYPE || method.getReturnType() == Void.class) {
            throw new ZLinkConfigurationException(
                "SPOT request handler method must return a reply: "
                    + handlerType.getName() + "." + method.getName());
        }
        return method.getReturnType();
    }

    private static String resolvePacketName(Class<?> messageType) {
        ZLinkPacket packet = messageType.getAnnotation(ZLinkPacket.class);
        return packet == null ? messageType.getSimpleName() : packet.value();
    }

    private static String resolvePacketName(Class<?> messageType, String explicitPacketName) {
        return explicitPacketName == null || explicitPacketName.isBlank()
            ? resolvePacketName(messageType)
            : explicitPacketName;
    }

    private static Class<?> requireHandlerType(Class<?> handlerType) {
        if (handlerType == null) {
            throw new ZLinkConfigurationException("SPOT handler type is required");
        }
        return handlerType;
    }

    private static String requireTopic(String topic) {
        if (topic == null || topic.isBlank()) {
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty");
        }
        return topic;
    }

    private final class SpotActivation implements AutoCloseable {
        private final ZLinkSpot spot;
        private final ZLinkBackendSpot backendSpot;
        private final DefaultSpotContext context;

        SpotActivation(
            ZLinkSpot spot,
            ZLinkBackendSpot backendSpot,
            DefaultSpotContext context) {
            this.spot = spot;
            this.backendSpot = backendSpot;
            this.context = context;
        }

        void handleDispatchEvent(ZLinkBackendSpotDispatchInfo info) {
            if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                drainRoutes();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE) {
                drainSubscriptions();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
                drainUnhandledActorJoins();
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                dispatchActorMessages(info.actorMessages());
            }
            if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE) {
                drainActorLifecycleEvents();
            }
            for (ZLinkBackendActorReceived actorMessage : info.actorMessages()) {
                actorMessage.close();
            }
        }

        private void drainRoutes() {
            while (true) {
                ZLinkBackendReceived received =
                    backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchRoute(received);
            }
        }

        private void dispatchRoute(ZLinkBackendReceived received) {
            if (received.parts().isEmpty()) {
                received.close();
                return;
            }
            ParsedPacket packet = parsePacket(received.parts());
            SpotPacketHandlerRegistration handler =
                context.packetHandler(packet.packetName());
            if (handler == null) {
                received.close();
                return;
            }
            if (received.requestSeq().isPresent()) {
                if (!handler.request()) {
                    received.close();
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotRequestHandler(handler, spot, payloadCopy)
                    .thenAccept(reply -> received.reply(List.of(reply)))
                    .whenComplete((ignored, error) -> {
                        payloadCopy.close();
                        received.close();
                    });
                return;
            }
            try (received) {
                if (handler.request()) {
                    return;
                }
                Message payloadCopy = Message.from(packet.payload());
                invokeSpotPacketHandler(handler, spot, payloadCopy)
                    .whenComplete((ignored, error) -> payloadCopy.close());
            }
        }

        private void drainSubscriptions() {
            while (true) {
                ZLinkBackendTopicMessage received =
                    backendSpot.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return;
                }
                dispatchSubscription(received);
            }
        }

        private void dispatchSubscription(ZLinkBackendTopicMessage received) {
            try {
                if (received.parts().isEmpty()) {
                    return;
                }
                ParsedPacket packet = parsePacket(received.parts());
                for (SpotSubscriptionHandlerRegistration handler :
                    context.subscriptionHandlers(received.topic())) {
                    if (!handler.packetName().equals(packet.packetName())) {
                        continue;
                    }
                    Message payloadCopy = Message.from(packet.payload());
                    invokeSpotSubscriptionHandler(handler, spot, payloadCopy)
                        .whenComplete((ignored, error) -> payloadCopy.close());
                }
            } finally {
                received.parts().forEach(Message::close);
            }
        }

        private void drainActorLifecycleEvents() {
            while (true) {
                ZLinkBackendActorLifecycleEvent event =
                    backendSpot.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
                if (event == null) {
                    return;
                }
                if (actorRuntime == null) {
                    continue;
                }
                ZLinkBackendActorRef actorRef = event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT
                    ? event.info().previousActor()
                    : event.info().currentActor();
                actorRuntime.localActor(actorRef.actorId())
                    .ifPresent(actor -> dispatchActorLifecycle(event, actorRef, actor));
            }
        }

        private void dispatchActorLifecycle(
            ZLinkBackendActorLifecycleEvent event,
            ZLinkBackendActorRef actorRef,
            ZLinkActor actor) {
            if (event.kind() == ZLinkBackendActorLifecycleEventKind.LEFT) {
                actorRuntime.markLeft(actor);
                ZLinkSpotActorChangeResult result =
                    new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.LEAVE_SPOT);
                actorRuntime.submitActorDispatch(
                    actor.actorId(),
                    () -> notifySpotActorLifecycle(actor, result, actorLeftHandlers));
                return;
            }
            RoutingId spotRid = event.info()
                .currentSpotRid()
                .orElse(backendSpot.routingId());
            actorRuntime.markJoined(actor, actorRef, spotRid, spotFor(spotRid));
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            actorRuntime.submitActorDispatch(
                actor.actorId(),
                () -> notifySpotActorLifecycle(actor, result, actorJoinedHandlers));
        }

        private void dispatchActorMessages(List<ZLinkBackendActorReceived> actorMessages) {
            int index = 0;
            while (index + 1 < actorMessages.size()) {
                ZLinkBackendActorReceived headerPart = actorMessages.get(index++);
                ZLinkBackendActorReceived bodyPart = actorMessages.get(index++);
                ActorPacketHeader packetHeader = decodeActorPacketHeader(headerPart);
                SpotActorPacketHandlerRegistration handler =
                    actorPacketHandlers.get(packetHeader.packetName());
                if (handler == null || actorRuntime == null) {
                    continue;
                }
                if (packetHeader.requestSeq().isPresent()
                    != (handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST)) {
                    continue;
                }
                actorRuntime.localActor(headerPart.actor().actorId())
                    .filter(actor -> handler.actorType().isInstance(actor))
                    .ifPresent(actor -> {
                        Message payloadCopy = Message.from(bodyPart.message());
                        actorRuntime.submitActorDispatch(
                            actor.actorId(),
                            () -> dispatchActorPacket(
                                handler,
                                actor,
                                packetHeader,
                                headerPart,
                                payloadCopy));
                    });
            }
        }

        private CompletionStage<Void> dispatchActorPacket(
            SpotActorPacketHandlerRegistration handler,
            ZLinkActor actor,
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived headerPart,
            Message payload) {
            CompletionStage<Optional<Message>> stage = handler.kind() == ZLinkScannedHandlerKind.ACTOR_REQUEST
                ? invokeActorRequestHandler(handler, actor, payload)
                : invokeActorSendHandler(handler, actor, payload).thenApply(ignored -> Optional.empty());
            return stage.thenAccept(reply -> {
                    if (reply.isEmpty()) {
                        return;
                    }
                    try (Message frame = encodeActorReplyFrame(packetHeader, headerPart, reply.get())) {
                        if (!primaryNode.sendActorBoundSession(
                            new ZLinkBackendActorRef(
                                headerPart.actor().nodeRid(),
                                actor.actorId(),
                                headerPart.actor().epoch()),
                            List.of(frame),
                            SendFlags.NONE)) {
                            throw new ZLinkConfigurationException(
                                "user actor bound session reply failed: " + actor.actorId());
                        }
                    }
                })
                .whenComplete((ignored, error) -> payload.close());
        }

        private ActorPacketHeader decodeActorPacketHeader(ZLinkBackendActorReceived headerPart) {
            byte[] bytes = headerPart.message().toByteArray();
            try {
                return decodeStreamActorPacketHeader(bytes, headerPart.requestSeq());
            } catch (RuntimeException ignored) {
                return new ActorPacketHeader(
                    headerPart.message().toUtf8String(),
                    headerPart.requestSeq(),
                    false,
                    0);
            }
        }

        private ActorPacketHeader decodeStreamActorPacketHeader(
            byte[] bytes,
            Optional<Long> backendRequestSeq) {
            if (bytes.length < 4) {
                throw new IllegalArgumentException("actor STREAM header is too short");
            }
            ByteBuffer buffer = ByteBuffer.wrap(bytes);
            int kind = Byte.toUnsignedInt(buffer.get());
            int codec = Byte.toUnsignedInt(buffer.get());
            int flags = Byte.toUnsignedInt(buffer.get());
            Optional<Long> requestSeq = backendRequestSeq;
            if ((flags & 0x01) != 0) {
                if (buffer.remaining() < Long.BYTES) {
                    throw new IllegalArgumentException("actor STREAM header missing request sequence");
                }
                long decodedRequestSeq = buffer.getLong();
                if (decodedRequestSeq == 0) {
                    throw new IllegalArgumentException("actor STREAM request sequence is zero");
                }
                requestSeq = Optional.of(decodedRequestSeq);
            }
            if (buffer.remaining() < 1) {
                throw new IllegalArgumentException("actor STREAM header missing packet name");
            }
            int nameLength = Byte.toUnsignedInt(buffer.get());
            if (buffer.remaining() < nameLength) {
                throw new IllegalArgumentException("actor STREAM header packet name is truncated");
            }
            byte[] nameBytes = new byte[nameLength];
            buffer.get(nameBytes);
            if ((flags & 0x02) != 0) {
                if (buffer.remaining() < 2) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                int metadataLength = Short.toUnsignedInt(buffer.getShort());
                if (buffer.remaining() < metadataLength) {
                    throw new IllegalArgumentException("actor STREAM header metadata is truncated");
                }
                buffer.position(buffer.position() + metadataLength);
            }
            if (buffer.hasRemaining()) {
                throw new IllegalArgumentException("actor STREAM header has trailing bytes");
            }
            if (kind != 1 && kind != 2) {
                throw new IllegalArgumentException("actor STREAM header is not dispatch kind");
            }
            return new ActorPacketHeader(
                new String(nameBytes, StandardCharsets.UTF_8),
                requestSeq,
                true,
                codec);
        }

        private Message encodeActorReplyFrame(
            ActorPacketHeader packetHeader,
            ZLinkBackendActorReceived originalHeader,
            Message payload) {
            if (!packetHeader.streamHeader() || packetHeader.requestSeq().isEmpty()) {
                return Message.from(payload);
            }
            byte[] name = packetHeader.packetName().getBytes(StandardCharsets.UTF_8);
            ByteBuffer header = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
            header.put((byte) 3);
            header.put((byte) packetHeader.codec());
            header.put((byte) 0x01);
            header.putLong(packetHeader.requestSeq().get());
            header.put((byte) name.length);
            header.put(name);
            byte[] headerBytes = header.array();
            byte[] body = payload.toByteArray();
            ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
            frame.putShort((short) headerBytes.length);
            frame.putInt(body.length);
            frame.put(headerBytes);
            frame.put(body);
            return Message.from(frame.array());
        }

        private record ActorPacketHeader(
            String packetName,
            Optional<Long> requestSeq,
            boolean streamHeader,
            int codec) {
        }

        private void drainUnhandledActorJoins() {
            while (true) {
                ZLinkBackendActorJoinRequest request =
                    backendSpot.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
                if (request == null) {
                    return;
                }
                try {
                    SpotActorJoinHandlerRegistration handler =
                        resolveActorJoinHandler(request);
                    if (handler == null) {
                        try (Message emptyReply = Message.from(new byte[0])) {
                            backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                        }
                        continue;
                    }
                    Message payload = actorJoinPayload(request);
                    invokeActorJoinHandler(handler, request, payload)
                        .whenComplete((reply, error) -> {
                            if (error != null) {
                                try (Message emptyReply = Message.from(new byte[0])) {
                                    backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                                }
                                return;
                            }
                            backendSpot.replyActorJoin(request, 0, List.of(reply));
                            reply.close();
                        });
                } finally {
                    request.parts().forEach(Message::close);
                }
            }
        }

        private SpotActorJoinHandlerRegistration resolveActorJoinHandler(
            ZLinkBackendActorJoinRequest request) {
            if (request.parts().isEmpty()) {
                return null;
            }
            if (request.parts().size() >= 2) {
                return actorJoinHandlers.get(request.parts().get(0).toUtf8String());
            }
            for (SpotActorJoinHandlerRegistration handler : actorJoinHandlers.values()) {
                if (handler.requestType() == Message.class
                    || handler.requestType() == byte[].class
                    || handler.requestType() == String.class) {
                    return handler;
                }
            }
            return null;
        }

        private Message actorJoinPayload(ZLinkBackendActorJoinRequest request) {
            return request.parts().size() >= 2 ? request.parts().get(1) : request.parts().get(0);
        }

        private CompletionStage<Message> invokeActorJoinHandler(
            SpotActorJoinHandlerRegistration registration,
            ZLinkBackendActorJoinRequest request,
            Message payload) {
            if (actorRuntime == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor runtime is required for Spot actor join handler"));
            }
            return actorRuntime
                .getOrCreateLocalActor(
                    request.targetActor().actorId(),
                    registration.actorType())
                .thenCompose(actor -> {
                    if (actor.isEmpty()) {
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "Spot actor join target actor is not available: "
                                + request.targetActor().actorId()));
                    }
                    actorRuntime.markJoined(
                        actor.get(),
                        request.targetActor(),
                        backendSpot.routingId(),
                        spotFor(backendSpot.routingId()));
                    try {
                        Object handler = handlerFactory.create(registration.handlerType());
                        Object requestObject =
                            serializer.deserialize(payload, registration.requestType());
                        Object result = registration.handlerMethod().invoke(
                            handler,
                            actor.get(),
                            requestObject);
                        CompletionStage<?> stage =
                            result instanceof CompletionStage<?> completionStage
                                ? completionStage
                                : CompletableFuture.completedFuture(result);
                        return stage
                            .thenCompose(reply -> notifyActorJoined(actor.get())
                                .thenApply(ignored -> reply))
                            .whenComplete((reply, error) -> {
                                if (error != null) {
                                    actorRuntime.markLeft(actor.get());
                                }
                            })
                            .thenApply(serializer::serialize);
                    } catch (IllegalAccessException | InvocationTargetException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                            "failed to invoke Spot actor join handler: "
                                + registration.handlerType().getName()
                                + "."
                                + registration.handlerMethod().getName(),
                            ex instanceof InvocationTargetException invocation
                                ? invocation.getCause()
                                : ex));
                    } catch (RuntimeException ex) {
                        actorRuntime.markLeft(actor.get());
                        return CompletableFuture.failedFuture(ex);
                    }
                });
        }

        private CompletionStage<Void> notifyActorJoined(ZLinkActor actor) {
            ZLinkSpotActorChangeResult result =
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JOIN_ENTRY_SPOT);
            return notifyActorLifecycle(actor, result, actorJoinedHandlers);
        }

        private CompletionStage<Void> notifyActorLifecycle(
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result,
            List<SpotActorLifecycleHandlerRegistration> registrations) {
            CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
            for (SpotActorLifecycleHandlerRegistration registration : registrations) {
                if (!registration.actorType().isInstance(actor)) {
                    continue;
                }
                tail = tail.thenCompose(ignored ->
                    invokeActorJoinedHandler(registration, actor, result));
            }
            return tail;
        }

        private CompletionStage<Void> invokeActorJoinedHandler(
            SpotActorLifecycleHandlerRegistration registration,
            ZLinkActor actor,
            ZLinkSpotActorChangeResult result) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object invocationResult =
                    registration.handlerMethod().invoke(handler, actor, result);
                if (invocationResult instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor joined handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Void> invokeActorSendHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(handler, actor, message);
                if (result instanceof CompletionStage<?> stage) {
                    return stage.thenApply(ignored -> null);
                }
                return CompletableFuture.completedFuture(null);
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor send handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        private CompletionStage<Optional<Message>> invokeActorRequestHandler(
            SpotActorPacketHandlerRegistration registration,
            ZLinkActor actor,
            Message payload) {
            try {
                Object handler = handlerFactory.create(registration.handlerType());
                Object message = serializer.deserialize(payload, registration.messageType());
                Object result = registration.handlerMethod().invoke(handler, actor, message);
                CompletionStage<?> stage = result instanceof CompletionStage<?> completionStage
                    ? completionStage
                    : CompletableFuture.completedFuture(result);
                return stage.thenApply(reply -> Optional.of(serializer.serialize(reply)));
            } catch (IllegalAccessException | InvocationTargetException ex) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "failed to invoke Spot actor request handler: "
                        + registration.handlerType().getName()
                        + "."
                        + registration.handlerMethod().getName(),
                    ex instanceof InvocationTargetException invocation
                        ? invocation.getCause()
                        : ex));
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }

        @Override
        public void close() {
            if (spot == null) {
                closeResources();
                return;
            }
            spot.onClosingAsync().whenComplete((ignored, error) -> closeResources());
        }

        private void closeResources() {
            context.closeTimers();
            backendSpot.close();
        }
    }
}
