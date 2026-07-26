package systems.zlink.framework.spots;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.locations.ZLinkAuthorityStore;

public final class ZLinkStoreSpotHandleResolver
    implements SpotHandleResolver, ActorSpotHandleResolver, SpotTransportAddressResolver {
    private final ZLinkStoreLocationResolvers.AddressResolvers addresses;

    public ZLinkStoreSpotHandleResolver(ZLinkStoreLocationResolvers.AddressResolvers addresses) {
        this(addresses, null);
    }

    public ZLinkStoreSpotHandleResolver(
        ZLinkStoreLocationResolvers.AddressResolvers addresses,
        ZLinkAuthorityStore authorities) {
        this.addresses = java.util.Objects.requireNonNull(addresses, "addresses");
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveSpotHandle(
        String meshName,
        String spotId) {
        return flowAware(addresses.resolveSpotRow(meshName, spotId))
            .thenApply(row -> row == null || !row.meshName().equals(meshName)
                ? Optional.empty()
                : Optional.<SpotHandle>of(new FrameworkSpotHandle(
                    row.meshName(), row.spotId(), row.nodeRid(),
                    row.spotGeneration())));
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveSpotHandle(String spotId) {
        return flowAware(addresses.resolveAnySpotRow(spotId))
            .thenApply(row -> row == null
                ? Optional.empty()
                : Optional.<SpotHandle>of(new FrameworkSpotHandle(
                    row.meshName(), row.spotId(), row.nodeRid(),
                    row.spotGeneration())));
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveActorSpotHandle(String actorId) {
        return flowAware(addresses.resolveActorSpotRow(actorId)).thenCompose(row -> {
            if (row == null) {
                return java.util.concurrent.CompletableFuture.completedFuture(Optional.empty());
            }
            String spotId = targetSpot(row.locationKind(), row.nodeRid(), row.spotId());
            return flowAware(addresses.resolveAnySpotRow(spotId)).thenApply(spot -> spot == null
                ? Optional.empty()
                : Optional.of(new FrameworkSpotHandle(
                    spot.meshName(), spot.spotId(), spot.nodeRid(), spot.spotGeneration())));
        });
    }

    @Override
    public CompletionStage<Optional<SpotTransportAddress>> resolve(SpotHandle handle) {
        return flowAware(addresses.resolveSpotRow(handle.meshName(), handle.spotId()))
            .thenApply(row -> row == null
                || row.spotGeneration()
                    != ((FrameworkSpotHandle) handle).spotGeneration()
                    ? Optional.empty()
                    : Optional.of(new SpotTransportAddress(
                        addresses.routerChannelId(row.meshName()),
                        row.nodeRid(),
                        row.spotId(),
                        row.spotGeneration(),
                        row.generation(),
                        row.spotKind())));
    }

    @Override
    public CompletionStage<Optional<SpotTransportAddress>> resolve(String spotId) {
        return resolveSpotHandle(spotId).thenCompose(handle -> handle
            .map(this::resolve)
            .orElseGet(() -> java.util.concurrent.CompletableFuture.completedFuture(
                Optional.empty())));
    }

    private static <T> CompletionStage<T> flowAware(CompletionStage<T> source) {
        return systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.propagate(source);
    }

    private static String targetSpot(
        ZLinkSpotKind kind,
        RoutingId nodeRid,
        String spotId) {
        return kind == ZLinkSpotKind.ENTRY || spotId == null
            ? nodeRid.toString()
            : spotId;
    }
}
