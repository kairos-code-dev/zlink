package systems.zlink.framework.runtime.registry;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.configuration.ZLinkRegistrySpotRemoteAddressesRegistration;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class ZLinkRegistrySpotRemoteAddressResolver
    implements ZLinkSpotRemoteAddressResolver {
    private final ZLinkFrameworkRuntime runtime;
    private final ZLinkFrameworkRegistration registration;

    public ZLinkRegistrySpotRemoteAddressResolver(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration) {
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.registration = Objects.requireNonNull(registration, "registration");
    }

    @Override
    public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(
        RoutingId spotRid) {
        ZLinkRegistrySpotRemoteAddressesRegistration options =
            registration.registrySpotRemoteAddresses();
        if (options == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "Registry SPOT routes are not configured."));
        }
        try {
            return CompletableFuture.completedFuture(
                runtime.resolveRegistrySpotRemoteAddress(
                    options.namespaceName(),
                    options.routerChannelId(),
                    spotRid));
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }
}
