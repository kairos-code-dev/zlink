package systems.zlink.framework.runtime;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.registry.ZLinkRegistryQueryFilter;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;

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
        ZLinkRegistryQueryFilter filter) {
        ZLinkRegistryQueryFilter resolved =
            filter == null ? ZLinkRegistryQueryFilter.all() : filter;
        ZLinkBackendRegistryQueryFilter backendFilter =
            new ZLinkBackendRegistryQueryFilter(resolved.channelName());
        return CompletableFuture.completedFuture(
            client.topology(backendFilter).stream()
                .map(entry -> new ZLinkRegistryTopologyEntry(
                    entry.channelName(),
                    entry.serviceKind(),
                    entry.endpoint()))
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
