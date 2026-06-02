package systems.zlink.framework.runtime;

import java.lang.reflect.Constructor;
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
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class ZLinkSpotRuntime implements ZLinkSpotManager, AutoCloseable {
    private final ZLinkBackendContext context;
    private final List<ZLinkBackendSpotNode> nodes = new ArrayList<>();
    private final Map<String, ZLinkBackendSpotNode> nodesByName = new HashMap<>();
    private final Set<Class<? extends ZLinkSpot>> registeredSpotTypes = new HashSet<>();
    private final Map<RoutingId, SpotActivation> spots = new HashMap<>();
    private final ZLinkBackendSpotNode primaryNode;
    private final ZLinkMessageSerializer serializer = new ZLinkStringMessageSerializer();
    private final ScheduledExecutorService timerExecutor = Executors.newScheduledThreadPool(1, task -> {
        Thread thread = new Thread(task, "zlink-java-spot-timer");
        thread.setDaemon(true);
        return thread;
    });

    public ZLinkSpotRuntime(
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkFrameworkRegistration registration) {
        if (registration.spotNodes().isEmpty()) {
            throw new ZLinkConfigurationException("at least one SpotNode is required");
        }
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkSpotBackendAdapter spotAdapter =
            backendFactory.createSpotAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        for (SpotNodeRegistration nodeRegistration : registration.spotNodes()) {
            ZLinkBackendSpotNode node =
                spotAdapter.createSpotNode(context, ZLinkBackendSpotNodeMode.ALL);
            if (nodeRegistration.routerBind() != null) {
                node.setRouterBind(nodeRegistration.routerBind());
            }
            if (nodeRegistration.pubBind() != null) {
                node.setPubBind(nodeRegistration.pubBind());
            }
            nodes.add(node);
            nodesByName.put(nodeRegistration.nodeName(), node);
            registeredSpotTypes.addAll(nodeRegistration.spotFactories());
        }
        this.primaryNode = nodes.get(0);
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
        spots.put(spotRid, activate(spotType, spot));
        return CompletableFuture.completedFuture(
            new ZLinkSpotCreateResult(spotRid, true));
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
        spots.put(spotRid, activate(spotType, spot));
        return CompletableFuture.completedFuture(
            new ZLinkSpotCreateResult(spotRid, true));
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
        for (SpotActivation spot : spots.values()) {
            spot.close();
        }
        spots.clear();
        timerExecutor.shutdownNow();
        for (ZLinkBackendSpotNode node : nodes) {
            node.close();
        }
        context.close();
    }

    ZLinkBackendSpotNode primaryNode() {
        return primaryNode;
    }

    Map<String, ZLinkBackendSpotNode> nodesByName() {
        return Map.copyOf(nodesByName);
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

    private SpotActivation activate(Class<? extends ZLinkSpot> spotType, ZLinkBackendSpot backendSpot) {
        DefaultSpotContext spotContext = new DefaultSpotContext(backendSpot);
        ZLinkSpot spot = tryCreateSpot(spotType, spotContext);
        if (spot == null) {
            return new SpotActivation(null, backendSpot, spotContext);
        }
        spotContext.setSpot(spot);
        spot.configure();
        spot.onCreateAsync(List.of()).toCompletableFuture().join();
        spot.onInitializeAsync().toCompletableFuture().join();
        return new SpotActivation(spot, backendSpot, spotContext);
    }

    private static ZLinkSpot tryCreateSpot(
        Class<? extends ZLinkSpot> spotType,
        ZLinkSpotContext context) {
        try {
            Constructor<? extends ZLinkSpot> contextConstructor =
                spotType.getDeclaredConstructor(ZLinkSpotContext.class);
            contextConstructor.setAccessible(true);
            return contextConstructor.newInstance(context);
        } catch (NoSuchMethodException ignored) {
            try {
                Constructor<? extends ZLinkSpot> constructor = spotType.getDeclaredConstructor();
                constructor.setAccessible(true);
                return constructor.newInstance();
            } catch (NoSuchMethodException ex) {
                return null;
            } catch (ReflectiveOperationException ex) {
                throw new ZLinkConfigurationException(
                    "failed to create spot: " + spotType.getName());
            }
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "failed to create spot: " + spotType.getName());
        }
    }

    private final class DefaultSpotContext implements ZLinkSpotContext {
        private static final CancellationToken NONE = () -> false;
        private final ZLinkBackendSpot backendSpot;
        private final List<ManagedTimer> timers = new ArrayList<>();
        private ZLinkSpot spot;

        DefaultSpotContext(ZLinkBackendSpot backendSpot) {
            this.backendSpot = backendSpot;
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
            return primaryNode.routingId();
        }

        @Override
        public ZLinkSpotOutbound outbound() {
            return new DefaultSpotOutbound(backendSpot);
        }

        @Override
        public CompletionStage<Void> leaveActorAsync(systems.zlink.framework.actors.ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
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

        private final class ManagedTimer implements ZLinkTimer {
            private final String name;
            private final Duration period;
            private final Class<?> handlerType;
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
                invokeTimerHandler(handlerType, spot, tick);
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
    private void invokeTimerHandler(Class<?> handlerType, ZLinkSpot spot, ZLinkTimerTick tick) {
        try {
            Object handler = handlerType.getDeclaredConstructor().newInstance();
            if (handler instanceof ZLinkSpotTimerHandler timerHandler) {
                timerHandler.handleAsync(spot, tick).toCompletableFuture().join();
            }
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "failed to create timer handler: " + handlerType.getName());
        }
    }

    private final class DefaultSpotOutbound implements ZLinkSpotOutbound {
        private final ZLinkBackendSpot backendSpot;

        DefaultSpotOutbound(ZLinkBackendSpot backendSpot) {
            this.backendSpot = backendSpot;
        }

        @Override
        public <TMessage> ZLinkSendCall sendToSpot(RoutingId spotRid, TMessage message) {
            throw new UnsupportedOperationException();
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToSpot(RoutingId spotRid, TMessage request) {
            throw new UnsupportedOperationException();
        }

        @Override
        public <TEvent> ZLinkPublishCall publish(String topic, TEvent message) {
            return new SpotPublishCall(backendSpot, topic, serializer.serialize(message), Optional.empty());
        }

        @Override
        public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
            throw new UnsupportedOperationException();
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage request) {
            throw new UnsupportedOperationException();
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
                List<Message> parts = packetName
                    .map(name -> List.of(Message.from(name.getBytes(StandardCharsets.UTF_8)), payload))
                    .orElseGet(() -> List.of(payload));
                try {
                    spot.publish(topic, parts, SendFlags.NONE);
                } finally {
                    parts.forEach(Message::close);
                }
            });
        }
    }

    private record SpotActivation(
        ZLinkSpot spot,
        ZLinkBackendSpot backendSpot,
        DefaultSpotContext context) implements AutoCloseable {
        @Override
        public void close() {
            try {
                if (spot != null) {
                    spot.onClosingAsync().toCompletableFuture().join();
                }
            } finally {
                context.closeTimers();
                backendSpot.close();
            }
        }
    }
}
