package systems.zlink.framework.spring;

import java.util.Objects;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;

public final class ZLinkFrameworkLifecycle
    implements SmartLifecycle, ZLinkClient, ZLinkFanoutClient, ZLinkRouteClient {
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
            runtime.close();
        } finally {
            runtime = null;
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
        return 0;
    }

    @Override
    public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
        return requireRuntime().client().sendToChannel(channelName, message);
    }

    @Override
    public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage message) {
        return requireRuntime().client().requestToChannel(channelName, message);
    }

    @Override
    public <TMessage> ZLinkPublishCall publish(
        String channelName,
        String topic,
        TMessage message) {
        return requireRuntime().fanout().publish(channelName, topic, message);
    }

    @Override
    public <TMessage> ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        TMessage message) {
        return requireRuntime().route().sendTo(channelName, target, message);
    }

    @Override
    public <TMessage> ZLinkRequestCall requestTo(
        String channelName,
        RoutingId target,
        TMessage message) {
        return requireRuntime().route().requestTo(channelName, target, message);
    }

    ZLinkSpotManager spotManager() {
        return requireRuntime().spotManager();
    }

    ZLinkSpotOutbound spotOutbound() {
        return requireRuntime().spotOutbound();
    }

    ZLinkSpotPublisherClient spotPublisherClient() {
        return requireRuntime().spotPublisherClient();
    }

    ZLinkSpotRemoteAddress resolveRegistrySpotRemoteAddress(
        String namespaceName,
        String configuredRouterChannelId,
        RoutingId spotRid) {
        return requireRuntime().resolveRegistrySpotRemoteAddress(
            namespaceName,
            configuredRouterChannelId,
            spotRid);
    }

    ZLinkActorManager actorManager() {
        return requireRuntime().actorManager();
    }

    private synchronized ZLinkFrameworkRuntime requireRuntime() {
        if (!running || runtime == null) {
            throw new ZLinkConfigurationException("ZLink framework runtime is not running");
        }
        return runtime;
    }
}
