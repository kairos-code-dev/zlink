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
        RoutingId spotRid) {
        return flowAware(addresses.resolveSpotRow(meshName, spotRid))
            .thenCompose(row -> row == null
                ? resolveAuthority(spotRid)
                    .thenApply(value -> value.filter(
                        resolved -> resolved.meshName().equals(meshName))
                        .map(SpotHandle.class::cast))
                : java.util.concurrent.CompletableFuture.completedFuture(
                    Optional.<SpotHandle>of(new FrameworkSpotHandle(
                        row.meshName(), row.spotRid(), row.nodeRid(),
                        row.spotGeneration()))));
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveSpotHandle(RoutingId spotRid) {
        return flowAware(addresses.resolveAnySpotRow(spotRid))
            .thenCompose(row -> row == null
                ? resolveAuthority(spotRid)
                    .thenApply(value -> value.map(SpotHandle.class::cast))
                : java.util.concurrent.CompletableFuture.completedFuture(
                    Optional.<SpotHandle>of(new FrameworkSpotHandle(
                        row.meshName(), row.spotRid(), row.nodeRid(),
                        row.spotGeneration()))));
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveActorSpotHandle(String actorId) {
        return flowAware(addresses.resolveActorSpotRow(actorId)).thenCompose(row -> {
            if (row == null) {
                return java.util.concurrent.CompletableFuture.completedFuture(Optional.empty());
            }
            RoutingId spotRid = targetSpot(row.locationKind(), row.nodeRid(), row.spotRid());
            return flowAware(addresses.resolveAnySpotRow(spotRid)).thenApply(spot -> spot == null
                ? Optional.empty()
                : Optional.of(new FrameworkSpotHandle(
                    spot.meshName(), spot.spotRid(), spot.nodeRid(), spot.spotGeneration())));
        });
    }

    @Override
    public CompletionStage<Optional<SpotTransportAddress>> resolve(SpotHandle handle) {
        return flowAware(addresses.resolveSpotRow(handle.meshName(), handle.spotRid()))
            .thenCompose(row -> row == null
                ? resolveAuthoritySnapshot(handle)
                : java.util.concurrent.CompletableFuture.completedFuture(
                    Optional.of(new SpotTransportAddress(
                        addresses.routerChannelId(row.meshName()),
                        row.nodeRid(),
                        row.spotRid(),
                        row.spotGeneration(),
                        row.generation(),
                        row.spotKind()))));
    }

    private CompletionStage<Optional<FrameworkSpotHandle>> resolveAuthority(
        RoutingId spotRid) {
        if (authorities == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                Optional.empty());
        }
        return authorities.read(
                ZLinkAuthorityKeyCodec.spot(spotRid), () -> false)
            .thenApply(read -> readyAuthority(read)
                .map(value -> new FrameworkSpotHandle(
                    value.authority().meshName(),
                    value.authority().spotRid(),
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
                ZLinkAuthorityKeyCodec.spot(handle.spotRid()), () -> false)
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
                    value.authority().spotRid(),
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

    private static RoutingId targetSpot(ZLinkSpotKind kind, RoutingId nodeRid, RoutingId spotRid) {
        return kind == ZLinkSpotKind.ENTRY || spotRid == null ? nodeRid : spotRid;
    }
}
