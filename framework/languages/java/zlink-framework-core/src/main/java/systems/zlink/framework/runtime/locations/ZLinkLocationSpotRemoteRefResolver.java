package systems.zlink.framework.runtime.locations;

import java.util.Objects;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.spots.SpotRemoteRef;
import systems.zlink.framework.spots.SpotRemoteRefResolver;

public final class ZLinkLocationSpotRemoteRefResolver
    implements SpotRemoteRefResolver {
    private final ZLinkStoreLocationResolvers.AddressResolvers addresses;

    public ZLinkLocationSpotRemoteRefResolver(
        ZLinkStoreLocationResolvers.AddressResolvers addresses) {
        this.addresses = Objects.requireNonNull(addresses, "addresses");
    }

    @Override
    public CompletionStage<SpotRemoteRef> resolveSpotRemoteRefAsync(
        RoutingId spotRid) {
        return addresses.resolveAnySpotRowAsync(spotRid)
            .thenApply(row -> {
                if (row == null) {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SPOT_ROUTE_NOT_FOUND,
                        "SPOT has no live location row in any registered spot mesh: " + spotRid);
                }
                return new SpotRemoteRef(
                    addresses.routerChannelId(row.meshName()),
                    row.nodeRid(),
                    row.spotRid(),
                    row.spotKind());
            });
    }
}
