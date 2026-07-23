package systems.zlink.framework.runtime.host;

import java.time.Duration;
import java.util.Map;
import java.util.Objects;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkFanoutPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;

public final class ZLinkFrameworkLifecycle
    implements SmartLifecycle, ZLinkClient, ZLinkFanoutClient, ZLinkRouteClient,
        ZLinkChannelRuntimeOptions,
        systems.zlink.framework.configuration.ZLinkMessageFlowControl,
        systems.zlink.framework.monitoring.ZLinkDrainControl {
    public static final int PHASE = 0;
    private static final Duration SPRING_SHUTDOWN_DRAIN_DEADLINE = Duration.ofSeconds(30);

    private final DefaultZLinkFrameworkOptions options;
    private final ZLinkBackendAdapterProvider backendAdapterFactory;
    private final ZLinkHandlerActivator handlerFactory;
    private final ZLinkRuntimeEventDispatcher eventDispatcher;
    private ZLinkFrameworkRuntime runtime;
    private boolean running;

    public ZLinkFrameworkLifecycle(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendAdapterFactory,
        ZLinkHandlerActivator handlerFactory) {
        this(options, backendAdapterFactory, handlerFactory, null);
    }

    public ZLinkFrameworkLifecycle(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendAdapterFactory,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        this.options = Objects.requireNonNull(options, "options");
        this.backendAdapterFactory = Objects.requireNonNull(
            backendAdapterFactory,
            "backendAdapterFactory");
        this.handlerFactory = Objects.requireNonNull(handlerFactory, "handlerFactory");
        this.eventDispatcher = eventDispatcher;
    }

    @Override
    public synchronized void start() {
        if (running) {
            return;
        }
        runtime = ZLinkFrameworkRuntime.start(
            options,
            backendAdapterFactory,
            handlerFactory,
            eventDispatcher);
        running = true;
    }

    @Override
    public synchronized void stop() {
        if (!running) {
            return;
        }
        ZLinkFrameworkRuntime current = runtime;
        if (current == null) {
            running = false;
            return;
        }
        current.shutdown(SPRING_SHUTDOWN_DRAIN_DEADLINE).whenComplete((result, failure) -> {
            synchronized (ZLinkFrameworkLifecycle.this) {
                if (runtime == current) {
                    runtime = null;
                }
                running = false;
            }
        });
    }

    @Override
    public void stop(Runnable callback) {
        ZLinkFrameworkRuntime current;
        synchronized (this) {
            current = runtime;
            if (!running || current == null) {
                callback.run();
                return;
            }
        }
        // Spring shutdown must not start maintenance relocation.
        current.shutdown(SPRING_SHUTDOWN_DRAIN_DEADLINE).whenComplete((result, failure) -> {
            synchronized (ZLinkFrameworkLifecycle.this) {
                runtime = null;
                running = false;
            }
            callback.run();
        });
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    @Override
    public boolean isAutoStartup() {
        return true;
    }

    @Override
    public int getPhase() {
        return PHASE;
    }

    @Override
    public void setMessageFlowMode(systems.zlink.framework.configuration.ZLinkMessageFlowLogMode mode) {
        requireRuntime().setMessageFlowMode(mode);
    }

    @Override
    public systems.zlink.framework.configuration.ZLinkMessageFlowLogMode messageFlowMode() {
        return requireRuntime().messageFlowMode();
    }

    @Override
    public ZLinkSendCall sendToChannel(String channelName, Object message) {
        return requireRuntime().client().sendToChannel(channelName, message);
    }

    @Override
    public ZLinkRequestCall requestToChannel(String channelName, Object message) {
        return requireRuntime().client().requestToChannel(channelName, message);
    }

    public systems.zlink.framework.spots.SpotHandleResolver spotHandleResolver() {
        return requireRuntime().spotHandleResolver();
    }

    public systems.zlink.framework.spots.ActorSpotHandleResolver actorSpotHandleResolver() {
        return requireRuntime().actorSpotHandleResolver();
    }

    @Override
    public ZLinkFanoutPublishCall publish(
        String channelName,
        Object message) {
        return requireRuntime().fanout().publish(channelName, message);
    }

    @Override
    public ZLinkSendCall sendToNode(
        String channelName,
        RoutingId target,
        Object message) {
        return requireRuntime().route().sendToNode(channelName, target, message);
    }

    @Override
    public ZLinkSendCall sendToSpot(
        SpotHandle spot,
        Object message) {
        return requireRuntime().route().sendToSpot(spot, message);
    }

    @Override
    public ZLinkRequestCall requestToNode(
        String channelName,
        RoutingId target,
        Object message) {
        return requireRuntime().route().requestToNode(channelName, target, message);
    }

    @Override
    public ZLinkRequestCall requestToSpot(
        SpotHandle spot,
        Object message) {
        return requireRuntime().route().requestToSpot(spot, message);
    }

    public ZLinkSpotManager spotManager() {
        return requireRuntime().spotManager();
    }

    public ZLinkSpotOutbound spotOutbound() {
        return requireRuntime().spotOutbound();
    }

    public ZLinkSpotPublisherClient spotPublisherClient() {
        return requireRuntime().spotPublisherClient();
    }

    public ZLinkActorManager actorManager() {
        return requireRuntime().actorManager();
    }

    public ZLinkActorDirectory actorDirectory() {
        return requireRuntime().actorDirectory();
    }

    public ZLinkActorClient actorClient() {
        return requireRuntime().actorClient();
    }

    @Override
    public systems.zlink.framework.channels.ZLinkClientServerChannelRuntimeOptions clientServerChannel(
        String channelName) {
        return requireRuntime().channelRuntimeOptions().clientServerChannel(channelName);
    }

    @Override
    public systems.zlink.framework.channels.ZLinkRouteMeshChannelRuntimeOptions routeMeshChannel(
        String channelName) {
        return requireRuntime().channelRuntimeOptions().routeMeshChannel(channelName);
    }

    public Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        return requireRuntime().monitoringSocketSources();
    }

    public Map<String, systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode>
    monitoringSpotSources() {
        return requireRuntime().monitoringSpotSources();
    }

    public ZLinkLocationRuntimeQuery monitoringLocationRuntimeQuery() {
        return requireRuntime().monitoringLocationRuntimeQuery();
    }

    public systems.zlink.framework.locations.ZLinkAllocatedRoutingIdProvider allocatedRoutingIds() {
        return requireRuntime().allocatedRoutingIds();
    }

    public boolean stopSpotRuntime() {
        return requireRuntime().stopSpotRuntime();
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> drain() {
        return requireRuntime().drain();
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> drain(
        java.time.Duration deadline) {
        return requireRuntime().drain(deadline);
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> awaitDrained() {
        return requireRuntime().awaitDrained();
    }

    @Override
    public boolean isReady() {
        return runtime != null && runtime.isReady();
    }

    private ZLinkFrameworkRuntime requireRuntime() {
        if (runtime == null) {
            throw new ZLinkConfigurationException("ZLink framework runtime is not running");
        }
        return runtime;
    }

}
