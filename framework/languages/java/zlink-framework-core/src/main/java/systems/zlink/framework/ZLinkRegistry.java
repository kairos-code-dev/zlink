package systems.zlink.framework;

import java.util.function.Consumer;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;

public final class ZLinkRegistry implements ZLinkRegistryQuery, AutoCloseable {
    private final ZLinkRegistryRuntime runtime;

    private ZLinkRegistry(ZLinkRegistryRuntime runtime) {
        this.runtime = runtime;
    }

    public static ZLinkRegistry start(
        Consumer<ZLinkEmbeddedRegistryOptions> configure) {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        configure.accept(options);
        return start(options, new ZLinkJavaBackendAdapterFactory());
    }

    public static ZLinkRegistry start(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkRegistry(ZLinkRegistryRuntime.start(options, backendFactory));
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.registry.ZLinkRegistryStatus>
        statusAsync() {
        return runtime.statusAsync();
    }

    @Override
    public java.util.concurrent.CompletionStage<java.util.List<systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry>>
        serviceSummaryAsync(systems.zlink.framework.registry.ZLinkRegistryQueryFilter filter) {
        return runtime.serviceSummaryAsync(filter);
    }

    @Override
    public java.util.concurrent.CompletionStage<java.util.List<systems.zlink.framework.registry.ZLinkRegistryTopologyEntry>>
        topologyAsync(systems.zlink.framework.registry.ZLinkRegistryQueryFilter filter) {
        return runtime.topologyAsync(filter);
    }

    @Override
    public void close() {
        runtime.close();
    }
}
