package systems.zlink.framework.runtime.host;

import java.util.Map;
import java.util.Objects;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;

public final class ZLinkFrameworkLifecycle
    implements SmartLifecycle, ZLinkClient, ZLinkFanoutClient, ZLinkRouteClient,
        ZLinkChannelRuntimeOptions,
        systems.zlink.framework.configuration.ZLinkMessageFlowControl {
    public static final int PHASE = 0;

    private final DefaultZLinkFrameworkOptions options;
    private final ZLinkBackendAdapterFactory backendAdapterFactory;
    private final ZLinkHandlerFactory handlerFactory;
    private ZLinkFrameworkRuntime runtime;
    private boolean running;

    public ZLinkFrameworkLifecycle(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkHandlerFactory handlerFactory) {
        this.options = Objects.requireNonNull(options, "options");
        this.backendAdapterFactory = Objects.requireNonNull(
            backendAdapterFactory,
            "backendAdapterFactory");
        this.handlerFactory = Objects.requireNonNull(handlerFactory, "handlerFactory");
    }

    @Override
    public synchronized void start() {
        if (running) {
            return;
        }
        runtime = ZLinkFrameworkRuntime.start(options, backendAdapterFactory, handlerFactory);
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
    public ZLinkRequestCall requestToChannel(String channelName, Object message) {
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
    public ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        Object message) {
        return requireRuntime().route().sendTo(channelName, target, message);
    }

    @Override
    public ZLinkRequestCall requestTo(
        String channelName,
        RoutingId target,
        Object message) {
        return requireRuntime().route().requestTo(channelName, target, message);
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

    public ZLinkSpotRemoteAddress resolveRegistrySpotRemoteAddress(
        String namespaceName,
        String configuredRouterChannelId,
        RoutingId spotRid) {
        return requireRuntime().resolveRegistrySpotRemoteAddress(
            namespaceName,
            configuredRouterChannelId,
            spotRid);
    }

    public ZLinkActorManager actorManager() {
        return requireRuntime().actorManager();
    }

    @Override
    public systems.zlink.framework.channels.ZLinkClientServerChannelRuntimeOptions clientServerChannel(
        String channelName) {
        return requireRuntime().channelRuntimeOptions().clientServerChannel(channelName);
    }

    public Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        return requireRuntime().monitoringSocketSources();
    }

    public Map<String, ZLinkBackendSpotNode> monitoringSpotSources() {
        return requireRuntime().monitoringSpotSources();
    }

    private ZLinkFrameworkRuntime requireRuntime() {
        if (runtime == null) {
            throw new ZLinkConfigurationException("ZLink framework runtime is not running");
        }
        return runtime;
    }

}
