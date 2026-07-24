package systems.zlink.framework.spots;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.locations.ZLinkAuthoritySnapshot;
import systems.zlink.framework.locations.ZLinkAuthorityStore;
import systems.zlink.framework.locations.ZLinkPlacementAllocationState;
import systems.zlink.framework.locations.ZLinkPlacementObjectKind;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.runtime.locations.ZLinkServiceAuthorityPayloadCodec;

public final class ZLinkStoreSpotHandleResolver
    implements SpotHandleResolver, ActorSpotHandleResolver, SpotTransportAddressResolver {
    private final ZLinkStoreLocationResolvers.AddressResolvers addresses;
    private final ZLinkAuthorityStore authorities;
    private final ZLinkServiceAuthorityPayloadCodec authorityPayloads =
        new ZLinkServiceAuthorityPayloadCodec();

    public ZLinkStoreSpotHandleResolver(ZLinkStoreLocationResolvers.AddressResolvers addresses) {
        this(addresses, null);
    }

    public ZLinkStoreSpotHandleResolver(
        ZLinkStoreLocationResolvers.AddressResolvers addresses,
        ZLinkAuthorityStore authorities) {
        this.addresses = java.util.Objects.requireNonNull(addresses, "addresses");
        this.authorities = authorities;
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveSpotHandle(
        String meshName,
        String spotId) {
        return flowAware(addresses.resolveSpotRow(meshName, spotId))
            .thenCompose(row -> row == null
                ? resolveAuthority(spotId)
                    .thenApply(value -> value.filter(
                        resolved -> resolved.meshName().equals(meshName))
                        .map(SpotHandle.class::cast))
                : java.util.concurrent.CompletableFuture.completedFuture(
                    Optional.<SpotHandle>of(new FrameworkSpotHandle(
                        row.meshName(), row.spotId(), row.nodeRid(),
                        row.spotGeneration()))));
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveSpotHandle(String spotId) {
        return flowAware(addresses.resolveAnySpotRow(spotId))
            .thenCompose(row -> row == null
                ? resolveAuthority(spotId)
                    .thenApply(value -> value.map(SpotHandle.class::cast))
                : java.util.concurrent.CompletableFuture.completedFuture(
                    Optional.<SpotHandle>of(new FrameworkSpotHandle(
                        row.meshName(), row.spotId(), row.nodeRid(),
                        row.spotGeneration()))));
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
            .thenCompose(row -> row == null
                ? resolveAuthoritySnapshot(handle)
                : java.util.concurrent.CompletableFuture.completedFuture(
                    Optional.of(new SpotTransportAddress(
                        addresses.routerChannelId(row.meshName()),
                        row.nodeRid(),
                        row.spotId(),
                        row.spotGeneration(),
                        row.generation(),
                        row.spotKind()))));
    }

    @Override
    public CompletionStage<Optional<SpotTransportAddress>> resolve(String spotId) {
        return resolveSpotHandle(spotId).thenCompose(handle -> handle
            .map(this::resolve)
            .orElseGet(() -> java.util.concurrent.CompletableFuture.completedFuture(
                Optional.empty())));
    }

    private CompletionStage<Optional<FrameworkSpotHandle>> resolveAuthority(
        String spotId) {
        if (authorities == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                Optional.empty());
        }
        return authorities.read(
                ZLinkAuthorityKeyCodec.spot(spotId), () -> false)
            .thenApply(read -> readyAuthority(read)
                .map(value -> new FrameworkSpotHandle(
                    value.authority().meshName(),
                    value.authority().spotId(),
                    value.authority().nodeRid(),
                    value.snapshot().objectGeneration())));
    }

    private CompletionStage<Optional<SpotTransportAddress>>
        resolveAuthoritySnapshot(SpotHandle handle) {
        if (authorities == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                Optional.empty());
        }
        return authorities.read(
                ZLinkAuthorityKeyCodec.spot(handle.spotId()), () -> false)
            .thenApply(read -> readyAuthority(read)
                .filter(value ->
                    value.authority().meshName().equals(handle.meshName())
                        && value.snapshot().objectGeneration()
                            == ((FrameworkSpotHandle) handle)
                                .spotGeneration())
                .map(value -> new SpotTransportAddress(
                    addresses.routerChannelId(
                        value.authority().meshName()),
                    value.authority().nodeRid(),
                    value.authority().spotId(),
                    value.snapshot().objectGeneration(),
                    value.snapshot().authorityOwnerGeneration(),
                    ZLinkSpotKind.USER)));
    }

    private Optional<ReadyAuthority> readyAuthority(Object read) {
        if (!(read instanceof ZLinkAuthoritySnapshot snapshot)
            || snapshot.allocation().state()
                != ZLinkPlacementAllocationState.ACTIVE
            || snapshot.allocation().objectKind()
                != ZLinkPlacementObjectKind.USER_SPOT) {
            return Optional.empty();
        }
        return authorityPayloads.decode(snapshot.payload())
            .filter(value ->
                value.kind() == ZLinkServiceAuthorityPayloadCodec.Kind.USER
                    && value.state()
                        == ZLinkServiceAuthorityPayloadCodec.State.READY
                    && value.ownerId().equals(snapshot.ownerId())
                    && value.ownerLeaseGeneration()
                        == snapshot.ownerLeaseGeneration())
            .map(value -> new ReadyAuthority(snapshot, value));
    }

    private record ReadyAuthority(
        ZLinkAuthoritySnapshot snapshot,
        ZLinkServiceAuthorityPayloadCodec.SpotAuthority authority) {
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
