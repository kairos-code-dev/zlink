package systems.zlink.framework.runtime.registry;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryQueryFilter;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryStatus;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkRegistryBackendAdapter;

public final class ZLinkRegistryRuntime implements ZLinkRegistryQuery, AutoCloseable {
    private final ZLinkBackendContext context;
    private final ZLinkBackendRegistry registry;

    public ZLinkRegistryRuntime(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions) {
        options.validate();
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkRegistryBackendAdapter registryAdapter =
            backendFactory.createRegistryAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        this.registry = registryAdapter.createRegistry(context);
        if (options.registryId() != 0) {
            registry.setId(options.registryId());
        }
        registry.bind(options.pubEndpoint(), options.routerEndpoint());
        for (String peerPubEndpoint : options.peerPubEndpoints()) {
            registry.connectPeer(peerPubEndpoint, "");
        }
    }

    public static ZLinkRegistryRuntime start(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkRegistryRuntime(
            options,
            backendFactory,
            new ZLinkBackendAdapterOptions(java.time.Duration.ofSeconds(30)));
    }

    @Override
    public CompletionStage<ZLinkRegistryStatus> statusAsync() {
        ZLinkBackendRegistryStatus status = registry.status();
        return CompletableFuture.completedFuture(
            new ZLinkRegistryStatus(
                status.registryId(),
                status.bindEndpoint(),
                status.state(),
                status.topologyEntryCount(),
                status.peerRegistryCount(),
                status.connectedPeerRegistryCount(),
                status.listSeq(),
                status.lastError(),
                status.lastChangedMs()));
    }

    @Override
    public CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummaryAsync(
        ZLinkRegistryQueryFilter filter) {
        return CompletableFuture.completedFuture(
            registry.serviceSummary(toBackendFilter(filter)).stream()
                .map(entry -> new ZLinkRegistryServiceSummaryEntry(
                    entry.channelName(),
                    entry.serviceKind(),
                    entry.serviceCount()))
                .toList());
    }

    @Override
    public CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryQueryFilter filter) {
        return CompletableFuture.completedFuture(
            registry.topology(toBackendFilter(filter)).stream()
                .map(entry -> new ZLinkRegistryTopologyEntry(
                    entry.channelName(),
                    entry.routingId(),
                    entry.serviceKind(),
                    entry.endpoint()))
                .toList());
    }

    @Override
    public void close() {
        try {
            registry.close();
        } finally {
            context.close();
        }
    }

    private static ZLinkBackendRegistryQueryFilter toBackendFilter(
        ZLinkRegistryQueryFilter filter) {
        ZLinkRegistryQueryFilter resolved =
            filter == null ? ZLinkRegistryQueryFilter.all() : filter;
        return new ZLinkBackendRegistryQueryFilter(resolved.channelName());
    }
}
