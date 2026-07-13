package systems.zlink.framework.spots;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;

public final class ZLinkStoreSpotHandleResolver
    implements SpotHandleResolver, ActorSpotHandleResolver, SpotTransportAddressResolver {
    private final ZLinkStoreLocationResolvers.AddressResolvers addresses;

    public ZLinkStoreSpotHandleResolver(ZLinkStoreLocationResolvers.AddressResolvers addresses) {
        this.addresses = java.util.Objects.requireNonNull(addresses, "addresses");
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveSpotHandle(RoutingId spotRid) {
        return flowAware(addresses.resolveAnySpotRow(spotRid)).thenApply(row -> row == null
            ? Optional.empty()
            : Optional.of(new FrameworkSpotHandle(row.spotRid())));
    }

    @Override
    public CompletionStage<Optional<SpotHandle>> resolveActorSpotHandle(String actorId) {
        return flowAware(addresses.resolveActorSpotRow(actorId)).thenApply(row -> row == null
            ? Optional.empty()
            : Optional.of(new FrameworkSpotHandle(targetSpot(
                row.locationKind(), row.nodeRid(), row.spotRid()))));
    }

    @Override
    public CompletionStage<Optional<SpotTransportAddress>> resolve(SpotHandle handle) {
        return flowAware(addresses.resolveAnySpotRow(handle.spotRid())).thenApply(row -> row == null
            ? Optional.empty()
            : Optional.of(new SpotTransportAddress(
                addresses.routerChannelId(row.meshName()), row.nodeRid(), row.spotRid(), row.spotKind())));
    }

    private static <T> CompletionStage<T> flowAware(CompletionStage<T> source) {
        return systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.propagate(source);
    }

    private static RoutingId targetSpot(ZLinkSpotKind kind, RoutingId nodeRid, RoutingId spotRid) {
        return kind == ZLinkSpotKind.ENTRY || spotRid == null ? nodeRid : spotRid;
    }
}
