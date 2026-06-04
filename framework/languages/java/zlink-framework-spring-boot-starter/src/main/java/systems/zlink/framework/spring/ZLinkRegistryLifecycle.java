package systems.zlink.framework.spring;

import java.util.Objects;
import java.util.List;
import java.util.concurrent.CompletionStage;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkMemberPeerEntry;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryFilter;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;

public final class ZLinkRegistryLifecycle implements SmartLifecycle, ZLinkRegistryQuery {
    public static final int PHASE = -100;

    private final ZLinkEmbeddedRegistryOptions options;
    private final ZLinkBackendAdapterFactory backendAdapterFactory;
    private ZLinkRegistryRuntime runtime;
    private boolean running;

    public ZLinkRegistryLifecycle(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendAdapterFactory) {
        this.options = Objects.requireNonNull(options, "options");
        this.backendAdapterFactory = Objects.requireNonNull(
            backendAdapterFactory,
            "backendAdapterFactory");
    }

    @Override
    public synchronized void start() {
        if (running) {
            return;
        }
        runtime = ZLinkRegistryRuntime.start(options, backendAdapterFactory);
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
        return PHASE;
    }

    @Override
    public CompletionStage<ZLinkRegistryStatus> statusAsync() {
        return requireRuntime().statusAsync();
    }

    @Override
    public CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummaryAsync(
        ZLinkRegistryServiceSummaryFilter filter) {
        return requireRuntime().serviceSummaryAsync(filter);
    }

    @Override
    public CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryTopologyFilter filter) {
        return requireRuntime().topologyAsync(filter);
    }

    @Override
    public CompletionStage<List<ZLinkMemberPeerEntry>> memberPeersAsync(String channelName) {
        return requireRuntime().memberPeersAsync(channelName);
    }

    private synchronized ZLinkRegistryRuntime requireRuntime() {
        if (!running || runtime == null) {
            throw new ZLinkConfigurationException("ZLink registry runtime is not running");
        }
        return runtime;
    }
}
