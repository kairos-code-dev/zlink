package systems.zlink.framework.runtime.registry;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.configuration.ZLinkRegistrySpotRemoteAddressesRegistration;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class ZLinkRegistrySpotRemoteAddressResolver
    implements ZLinkSpotRemoteAddressResolver {
    private static final ScheduledExecutorService RETRY_EXECUTOR =
        Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-registry-spot-resolver");
            thread.setDaemon(true);
            return thread;
        });

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
        CompletableFuture<ZLinkSpotRemoteAddress> result = new CompletableFuture<>();
        long deadline = System.nanoTime() + registration.defaultTimeout().toNanos();
        class Attempt implements Runnable {
            private RuntimeException lastError;

            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                try {
                    result.complete(resolveOnce(spotRid));
                    return;
                } catch (RuntimeException ex) {
                    lastError = ex;
                }
                if (System.nanoTime() >= deadline) {
                    result.completeExceptionally(lastError);
                    return;
                }
                RETRY_EXECUTOR.schedule(this, 25, TimeUnit.MILLISECONDS);
            }
        }
        new Attempt().run();
        return result;
    }

    private ZLinkSpotRemoteAddress resolveOnce(RoutingId spotRid) {
        ZLinkRegistrySpotRemoteAddressesRegistration options =
            registration.registrySpotRemoteAddresses();
        if (options == null) {
            throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        }
        return runtime.resolveRegistrySpotRemoteAddress(
            options.namespaceName(),
            options.routerChannelId(),
            spotRid);
    }
}
