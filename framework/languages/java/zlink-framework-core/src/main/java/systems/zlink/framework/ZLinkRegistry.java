package systems.zlink.framework;

import java.util.function.Consumer;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkMemberPeerEntry;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryFilter;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;

public final class ZLinkRegistry implements ZLinkRegistryQuery, AutoCloseable {
    private final ZLinkRegistryRuntime runtime;

    private ZLinkRegistry(ZLinkRegistryRuntime runtime) {
        this.runtime = runtime;
    }

    public static ZLinkRegistry start(
        Consumer<ZLinkEmbeddedRegistryOptions> configure) {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        configure.accept(options);
        return start(options, new ZLinkJavaBackendAdapterFactory());
    }

    public static ZLinkRegistry start(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkRegistry(ZLinkRegistryRuntime.start(options, backendFactory));
    }

    @Override
    public CompletionStage<ZLinkRegistryStatus> statusAsync() {
        return runtime.statusAsync();
    }

    @Override
    public CompletionStage<List<ZLinkRegistryServiceSummaryEntry>>
        serviceSummaryAsync(ZLinkRegistryServiceSummaryFilter filter) {
        return runtime.serviceSummaryAsync(filter);
    }

    @Override
    public CompletionStage<List<ZLinkRegistryTopologyEntry>>
        topologyAsync(ZLinkRegistryTopologyFilter filter) {
        return runtime.topologyAsync(filter);
    }

    @Override
    public CompletionStage<List<ZLinkMemberPeerEntry>> memberPeersAsync(String channelName) {
        return runtime.memberPeersAsync(channelName);
    }

    @Override
    public void close() {
        runtime.close();
    }
}
