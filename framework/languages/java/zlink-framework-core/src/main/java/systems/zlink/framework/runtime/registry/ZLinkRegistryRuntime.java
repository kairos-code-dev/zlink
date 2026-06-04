package systems.zlink.framework.runtime.registry;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkMemberPeerEntry;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryFilter;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryMemberPeerEntry;
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
        ZLinkRegistryServiceSummaryFilter filter) {
        return CompletableFuture.completedFuture(
            registry.serviceSummary(toBackendFilter(filter)).stream()
                .map(entry -> new ZLinkRegistryServiceSummaryEntry(
                    entry.autoConnectType(),
                    entry.serviceRole(),
                    entry.channelName(),
                    entry.totalCount(),
                    entry.connectingCount(),
                    entry.readyCount(),
                    entry.errorCount(),
                    entry.stoppedCount(),
                    entry.lastReportedMs()))
                .toList());
    }

    @Override
    public CompletionStage<List<ZLinkRegistryTopologyEntry>> topologyAsync(
        ZLinkRegistryTopologyFilter filter) {
        return CompletableFuture.completedFuture(
            registry.topology(toBackendFilter(filter)).stream()
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
    public CompletionStage<List<ZLinkMemberPeerEntry>> memberPeersAsync(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new IllegalArgumentException("channelName is required");
        }
        return CompletableFuture.completedFuture(
            registry.memberPeers(channelName).stream()
                .map(ZLinkRegistryRuntime::toMemberPeer)
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
        ZLinkRegistryServiceSummaryFilter filter) {
        ZLinkRegistryServiceSummaryFilter resolved =
            filter == null ? ZLinkRegistryServiceSummaryFilter.all() : filter;
        return new ZLinkBackendRegistryQueryFilter(
            resolved.autoConnectType(),
            Optional.empty(),
            resolved.serviceRole(),
            resolved.channelName(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty());
    }

    private static ZLinkBackendRegistryQueryFilter toBackendFilter(
        ZLinkRegistryTopologyFilter filter) {
        ZLinkRegistryTopologyFilter resolved =
            filter == null ? ZLinkRegistryTopologyFilter.all() : filter;
        return new ZLinkBackendRegistryQueryFilter(
            resolved.autoConnectType(),
            resolved.serviceKind(),
            resolved.serviceRole(),
            resolved.channelName(),
            resolved.routingId(),
            resolved.state(),
            resolved.source());
    }

    private static ZLinkMemberPeerEntry toMemberPeer(
        ZLinkBackendRegistryMemberPeerEntry entry) {
        return new ZLinkMemberPeerEntry(
            entry.autoConnectType(),
            entry.serviceRole(),
            entry.channelName(),
            entry.endpoint(),
            entry.routingId(),
            entry.value(),
            entry.weight());
    }
}
