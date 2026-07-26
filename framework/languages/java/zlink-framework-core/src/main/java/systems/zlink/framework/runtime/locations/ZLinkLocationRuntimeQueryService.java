package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.ZLinkLocationRuntimeStatus;
import systems.zlink.framework.locations.ZLinkLocationServiceSummary;
import systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter;
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry;
import systems.zlink.framework.locations.ZLinkLocationTopologyFilter;
import systems.zlink.framework.locations.ZLinkLocationTopologyState;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;

public final class ZLinkLocationRuntimeQueryService implements ZLinkLocationRuntimeQuery {
    private final ZLinkRegisteredLocationStores stores;
    private final ZLinkLocationRuntime runtime;
    private final ZLinkLocationOptions options;
    private final ZLinkLiveLocationRows liveRows;

    public ZLinkLocationRuntimeQueryService(
        ZLinkRegisteredLocationStores stores,
        ZLinkLocationRuntime runtime,
        ZLinkLocationOptions options) {
        this(stores, runtime, options, ZLinkLiveLocationRows.create(stores, options));
    }

    public ZLinkLocationRuntimeQueryService(
        ZLinkRegisteredLocationStores stores,
        ZLinkLocationRuntime runtime,
        ZLinkLocationOptions options,
        ZLinkLiveLocationRows liveRows) {
        this.stores = Objects.requireNonNull(stores, "stores");
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.options = Objects.requireNonNull(options, "options");
        this.liveRows = Objects.requireNonNull(liveRows, "liveRows");
    }

    @Override
    public CompletionStage<ZLinkLocationRuntimeStatus> getStatus() {
        return CompletableFuture.completedFuture(new ZLinkLocationRuntimeStatus(
            runtime.lastError() == null,
            stores.watchStore() != null,
            options.pollingInterval(),
            runtime.ownerLeaseRenewedAt(),
            runtime.lastError(),
            runtime.ownerLeaseHealthy(),
            runtime.ownerLeaseRenewedAt()));
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter) {
        return stores.peerStore().listPeerLocations(filter)
            .thenCompose(liveRows::filterLivePeers);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return stores.spotStore().listSpotLocations(filter, normalize(page))
            .thenCompose(raw -> liveRows.filterLiveSpots(raw.items())
                .thenApply(items -> new ZLinkLocationPage<>(items, raw.continuationToken())));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return stores.actorStore().listActorLocations(filter, normalize(page))
            .thenCompose(raw -> liveRows.filterLiveActors(raw.items())
                .thenApply(items -> new ZLinkLocationPage<>(items, raw.continuationToken())));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return stores.routeStore().listRouteLocations(filter, normalize(page))
            .thenCompose(raw -> liveRows.filterLiveRoutes(raw.items())
                .thenApply(items -> new ZLinkLocationPage<>(items, raw.continuationToken())));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listTopology(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page) {
        ZLinkLocationTopologyFilter safeFilter =
            filter == null ? ZLinkLocationTopologyFilter.all() : filter;
        if (safeFilter.kind() == null) {
            return listAllTopologyKinds(safeFilter, page);
        }
        return switch (safeFilter.kind()) {
            case INVALID -> CompletableFuture.completedFuture(new ZLinkLocationPage<>(List.of(), null));
            case PEER -> listPeerTopology(safeFilter, page);
            case SPOT -> listSpotLocations(
                    new ZLinkSpotLocationFilter(safeFilter.meshName(), null, safeFilter.nodeRid(), null),
                    page)
                .thenApply(spots -> new ZLinkLocationPage<>(
                    spots.items().stream()
                        .map(row -> new ZLinkLocationTopologyEntry(
                            ZLinkLocationKind.SPOT,
                            row.meshName(),
                            null,
                            row.nodeRid(),
                            row.spotId(),
                            null,
                            row.routeEndpoint(),
                            ZLinkLocationTopologyState.READY,
                            1,
                            1,
                            0,
                            row.updatedAt()))
                        .filter(entry -> matches(entry, safeFilter))
                        .toList(),
                    spots.continuationToken()));
            case ACTOR -> listActorLocations(
                    new ZLinkActorLocationFilter(null, safeFilter.nodeRid(), null, null),
                    page)
                .thenApply(actors -> new ZLinkLocationPage<>(
                    actors.items().stream()
                        .map(row -> new ZLinkLocationTopologyEntry(
                            ZLinkLocationKind.ACTOR,
                            row.spotMeshName(),
                            null,
                            row.nodeRid(),
                            row.spotId(),
                            row.actorId(),
                            null,
                            ZLinkLocationTopologyState.READY,
                            1,
                            1,
                            0,
                            row.updatedAt()))
                        .filter(entry -> matches(entry, safeFilter))
                        .toList(),
                    actors.continuationToken()));
            case ROUTE -> listRouteLocations(new ZLinkRouteLocationFilter(null, safeFilter.nodeRid(), null), page)
                .thenApply(routes -> new ZLinkLocationPage<>(
                    routes.items().stream()
                        .map(row -> new ZLinkLocationTopologyEntry(
                            ZLinkLocationKind.ROUTE,
                            null,
                            null,
                            row.ownerNodeRid(),
                            null,
                            null,
                            row.routeKey(),
                            ZLinkLocationTopologyState.READY,
                            1,
                            1,
                            0,
                            row.updatedAt()))
                        .filter(entry -> matches(entry, safeFilter))
                        .toList(),
                    routes.continuationToken()));
        };
    }

    private CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listAllTopologyKinds(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page) {
        ZLinkPageRequest safePage = normalize(page);
        TopologyCursor cursor = decodeTopologyCursor(safePage.continuationToken());
        return collectTopologyKinds(
            filter,
            cursor.kindIndex(),
            cursor.storeToken(),
            safePage.pageSize(),
            new ArrayList<>());
    }

    private CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> collectTopologyKinds(
        ZLinkLocationTopologyFilter filter,
        int kindIndex,
        String storeToken,
        int pageSize,
        List<ZLinkLocationTopologyEntry> entries) {
        ZLinkLocationKind[] kinds = {
            ZLinkLocationKind.PEER,
            ZLinkLocationKind.SPOT,
            ZLinkLocationKind.ACTOR,
            ZLinkLocationKind.ROUTE
        };
        if (kindIndex >= kinds.length) {
            return CompletableFuture.completedFuture(new ZLinkLocationPage<>(entries, null));
        }
        int remaining = pageSize - entries.size();
        ZLinkLocationTopologyFilter kindFilter = new ZLinkLocationTopologyFilter(
            kinds[kindIndex],
            filter.meshName(),
            filter.role(),
            filter.nodeRid(),
            filter.state());
        return listTopology(kindFilter, new ZLinkPageRequest(remaining, storeToken))
            .thenCompose(kindPage -> {
                entries.addAll(kindPage.items());
                String nextStoreToken = kindPage.continuationToken();
                if (entries.size() >= pageSize) {
                    String continuation = nextStoreToken == null
                        ? (kindIndex + 1 < kinds.length
                            ? encodeTopologyCursor(kindIndex + 1, null)
                            : null)
                        : encodeTopologyCursor(kindIndex, nextStoreToken);
                    return CompletableFuture.completedFuture(
                        new ZLinkLocationPage<>(List.copyOf(entries), continuation));
                }
                return collectTopologyKinds(
                    filter,
                    nextStoreToken == null ? kindIndex + 1 : kindIndex,
                    nextStoreToken,
                    pageSize,
                    entries);
            });
    }

    private static String encodeTopologyCursor(int kindIndex, String storeToken) {
        String encodedStoreToken = storeToken == null
            ? ""
            : Base64.getUrlEncoder().withoutPadding().encodeToString(
                storeToken.getBytes(StandardCharsets.UTF_8));
        return "topology:" + kindIndex + ":" + encodedStoreToken;
    }

    private static TopologyCursor decodeTopologyCursor(String token) {
        if (token == null || !token.startsWith("topology:")) {
            return new TopologyCursor(0, null);
        }
        String[] fields = token.split(":", 3);
        try {
            int kindIndex = Integer.parseInt(fields[1]);
            String storeToken = fields.length < 3 || fields[2].isEmpty()
                ? null
                : new String(Base64.getUrlDecoder().decode(fields[2]), StandardCharsets.UTF_8);
            return new TopologyCursor(Math.max(0, kindIndex), storeToken);
        } catch (IllegalArgumentException ignored) {
            return new TopologyCursor(0, null);
        }
    }

    @Override
    public CompletionStage<List<ZLinkLocationServiceSummary>> listServiceSummaries(
        ZLinkLocationServiceSummaryFilter filter) {
        ZLinkLocationServiceSummaryFilter safeFilter =
            filter == null ? ZLinkLocationServiceSummaryFilter.all() : filter;
        return listPeerLocations(new ZLinkPeerLocationFilter(
                safeFilter.autoConnectType(),
                safeFilter.meshName(),
                safeFilter.role(),
                null,
                null))
            .thenApply(rows -> summarize(rows, safeFilter));
    }

    private CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listPeerTopology(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page) {
        return listPeerLocations(new ZLinkPeerLocationFilter(null, filter.meshName(), filter.role(), filter.nodeRid(), null))
            .thenApply(rows -> pageInMemory(rows.stream()
                .map(row -> new ZLinkLocationTopologyEntry(
                    ZLinkLocationKind.PEER,
                    row.meshName(),
                    row.role(),
                    row.nodeRid(),
                    null,
                    null,
                    row.endpoint(),
                    ZLinkLocationTopologyState.READY,
                    1,
                    1,
                    0,
                    row.updatedAt()))
                .filter(entry -> matches(entry, filter))
                .toList(), page));
    }

    private static List<ZLinkLocationServiceSummary> summarize(
        List<ZLinkPeerLocation> rows,
        ZLinkLocationServiceSummaryFilter filter) {
        Map<Group, MutableSummary> summaries = new LinkedHashMap<>();
        for (ZLinkPeerLocation row : rows) {
            Group group = new Group(row.meshName(), row.autoConnectType(), row.role());
            summaries.computeIfAbsent(group, ignored -> new MutableSummary()).add(row.updatedAt());
        }
        return summaries.entrySet().stream()
            .map(entry -> entry.getValue().toSummary(entry.getKey()))
            .filter(summary -> filter.meshName() == null || Objects.equals(summary.meshName(), filter.meshName()))
            .filter(summary -> filter.autoConnectType() == null || summary.autoConnectType() == filter.autoConnectType())
            .filter(summary -> filter.role() == null || summary.role() == filter.role())
            .sorted(Comparator.comparing(ZLinkLocationServiceSummary::meshName)
                .thenComparing(summary -> summary.autoConnectType().name())
                .thenComparing(summary -> summary.role().name()))
            .toList();
    }

    private static boolean matches(
        ZLinkLocationTopologyEntry entry,
        ZLinkLocationTopologyFilter filter) {
        return (filter.kind() == null || entry.kind() == filter.kind())
            && (filter.meshName() == null || Objects.equals(entry.meshName(), filter.meshName()))
            && (filter.role() == null || entry.role() == filter.role())
            && (filter.nodeRid() == null || Objects.equals(entry.nodeRid(), filter.nodeRid()))
            && (filter.state() == null || entry.state() == filter.state());
    }

    private <T> ZLinkLocationPage<T> pageInMemory(List<T> rows, ZLinkPageRequest page) {
        ZLinkPageRequest safePage = normalize(page);
        int offset = parseOffset(safePage.continuationToken());
        int limit = safePage.pageSize() <= 0 ? rows.size() : safePage.pageSize();
        List<T> items = rows.stream().skip(offset).limit(limit).toList();
        int nextOffset = offset + items.size();
        return new ZLinkLocationPage<>(
            items,
            nextOffset < rows.size() ? Integer.toString(nextOffset) : null);
    }

    private ZLinkPageRequest normalize(ZLinkPageRequest page) {
        ZLinkPageRequest safePage = page == null ? ZLinkPageRequest.firstPage() : page;
        return safePage.pageSize() > 0
            ? safePage
            : new ZLinkPageRequest(1000, safePage.continuationToken());
    }

    private static int parseOffset(String token) {
        if (token == null || token.isBlank()) {
            return 0;
        }
        try {
            return Math.max(0, Integer.parseInt(token));
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    private record Group(
        String meshName,
        systems.zlink.framework.locations.ZLinkLocationAutoConnectType autoConnectType,
        ZLinkLocationRole role) {
    }

    private record TopologyCursor(int kindIndex, String storeToken) {
    }

    private static final class MutableSummary {
        private long totalCount;
        private Instant lastUpdatedAt = Instant.EPOCH;

        void add(Instant updatedAt) {
            totalCount++;
            if (updatedAt != null && updatedAt.isAfter(lastUpdatedAt)) {
                lastUpdatedAt = updatedAt;
            }
        }

        ZLinkLocationServiceSummary toSummary(Group group) {
            return new ZLinkLocationServiceSummary(
                group.meshName(),
                group.autoConnectType(),
                group.role(),
                totalCount,
                totalCount,
                0,
                0,
                lastUpdatedAt);
        }
    }
}
