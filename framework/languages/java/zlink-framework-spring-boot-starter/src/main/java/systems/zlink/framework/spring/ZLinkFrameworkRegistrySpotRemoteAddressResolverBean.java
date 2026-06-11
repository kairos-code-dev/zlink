package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.configuration.ZLinkRegistrySpotRemoteAddressesRegistration;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

final class ZLinkFrameworkRegistrySpotRemoteAddressResolverBean
    implements ZLinkSpotRemoteAddressResolver {
    private final ZLinkFrameworkLifecycle lifecycle;
    private final ZLinkRegistrySpotRemoteAddressesRegistration registration;

    ZLinkFrameworkRegistrySpotRemoteAddressResolverBean(
        ZLinkFrameworkLifecycle lifecycle,
        DefaultZLinkFrameworkOptions options) {
        this.lifecycle = lifecycle;
        this.registration = options.registration().registrySpotRemoteAddresses();
    }

    @Override
    public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(
        RoutingId spotRid) {
        try {
            return CompletableFuture.completedFuture(
                lifecycle.resolveRegistrySpotRemoteAddress(
                    registration.namespaceName(),
                    registration.routerChannelId(),
                    spotRid));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }
}
