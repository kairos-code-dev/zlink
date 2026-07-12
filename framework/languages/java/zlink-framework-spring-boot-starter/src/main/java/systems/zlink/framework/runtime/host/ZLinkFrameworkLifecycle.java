package systems.zlink.framework.runtime.host;

import java.util.Map;
import java.util.Objects;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;

public final class ZLinkFrameworkLifecycle
    implements SmartLifecycle, ZLinkClient, ZLinkFanoutClient, ZLinkRouteClient,
        ZLinkChannelRuntimeOptions,
        systems.zlink.framework.configuration.ZLinkMessageFlowControl {
    public static final int PHASE = 0;

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
        try {
            if (runtime != null) {
                runtime.close();
                runtime = null;
            }
        } finally {
            running = false;
        }
    }

    @Override
    public void stop(Runnable callback) {
        try {
            stop();
        } finally {
            callback.run();
        }
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
    public ZLinkYieldRequestCall requestToChannel(String channelName, Object message) {
        return requireRuntime().client().requestToChannel(channelName, message);
    }

    @Override
    public ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message) {
        return requireRuntime().fanout().publish(channelName, topic, message);
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
        String channelName,
        SpotRef spotRef,
        Object message) {
        return requireRuntime().route().sendToSpot(
            channelName,
            spotRef,
            message);
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
        String channelName,
        SpotRef spotRef,
        Object message) {
        return requireRuntime().route().requestToSpot(
            channelName,
            spotRef,
            message);
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

    public Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        return requireRuntime().monitoringSocketSources();
    }

    public Map<String, ZLinkInternalSpotNode> monitoringSpotSources() {
        return requireRuntime().monitoringSpotSources();
    }

    public ZLinkLocationRuntimeQuery monitoringLocationRuntimeQuery() {
        return requireRuntime().monitoringLocationRuntimeQuery();
    }

    public boolean stopSpotRuntime() {
        return requireRuntime().stopSpotRuntime();
    }

    private ZLinkFrameworkRuntime requireRuntime() {
        if (runtime == null) {
            throw new ZLinkConfigurationException("ZLink framework runtime is not running");
        }
        return runtime;
    }

}
