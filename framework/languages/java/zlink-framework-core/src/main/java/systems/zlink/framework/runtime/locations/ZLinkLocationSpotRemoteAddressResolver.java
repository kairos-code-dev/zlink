package systems.zlink.framework.runtime.locations;

import java.util.Objects;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class ZLinkLocationSpotRemoteAddressResolver
    implements ZLinkSpotRemoteAddressResolver {
    private final ZLinkStoreLocationResolvers.AddressResolvers addresses;

    public ZLinkLocationSpotRemoteAddressResolver(
        ZLinkStoreLocationResolvers.AddressResolvers addresses) {
        this.addresses = Objects.requireNonNull(addresses, "addresses");
    }

    @Override
    public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(
        RoutingId spotRid) {
        return addresses.resolveAnySpotRowAsync(spotRid)
            .thenApply(row -> {
                if (row == null) {
                    throw new ZLinkFrameworkException(
                        "SPOT has no live location row in any registered spot mesh: " + spotRid);
                }
                return new ZLinkSpotRemoteAddress(
                    addresses.routerChannelId(row.meshName()),
                    row.nodeRid(),
                    row.spotRid(),
                    row.spotKind());
            });
    }
}
