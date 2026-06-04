package systems.zlink.framework.runtime.registry;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryFilter;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkRegistryBackendAdapter;

public final class ZLinkRemoteRegistryQueryClient implements ZLinkRegistryQueryClient {
    private final ZLinkBackendContext context;
    private final ZLinkBackendRegistryQueryClient client;

    public ZLinkRemoteRegistryQueryClient(
        String endpoint,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new IllegalArgumentException("endpoint is required");
        }
        ZLinkChannelBackendAdapter channelAdapter =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkRegistryBackendAdapter registryAdapter =
            backendFactory.createRegistryAdapter(adapterOptions);
        this.context = channelAdapter.createContext();
        this.client = registryAdapter.createRegistryQueryClient(context);
        client.connect(endpoint);
    }

    public static ZLinkRemoteRegistryQueryClient connect(
        String endpoint,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkRemoteRegistryQueryClient(
            endpoint,
            backendFactory,
            new ZLinkBackendAdapterOptions(Duration.ofSeconds(30)));
    }

    @Override
    public CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryTopologyFilter filter) {
        ZLinkRegistryTopologyFilter resolved =
            filter == null ? ZLinkRegistryTopologyFilter.all() : filter;
        ZLinkBackendRegistryQueryFilter backendFilter =
            new ZLinkBackendRegistryQueryFilter(
                resolved.autoConnectType(),
                resolved.serviceKind(),
                resolved.serviceRole(),
                resolved.channelName(),
                resolved.routingId(),
                resolved.state(),
                resolved.source());
        return CompletableFuture.completedFuture(
            client.topology(backendFilter).stream()
                .map(entry -> new ZLinkRegistryTopologyEntry(
                    entry.autoConnectType(),
                    entry.routingId(),
                    entry.serviceKind(),
                    entry.serviceRole(),
                    entry.channelName(),
                    entry.endpoint(),
                    entry.source(),
                    entry.state(),
                    entry.desiredCount(),
                    entry.readyCount(),
                    entry.errorCode(),
                    entry.lastReportedMs(),
                    entry.spotKind()))
                .toList());
    }

    @Override
    public void close() {
        try {
            client.close();
        } finally {
            context.close();
        }
    }
}
