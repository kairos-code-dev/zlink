package systems.zlink.framework;

import java.util.function.Consumer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.framework.runtime.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.spots.ZLinkSpotManager;

public final class ZLinkFramework implements AutoCloseable {
    private final ZLinkFrameworkRuntime runtime;

    private ZLinkFramework(ZLinkFrameworkRuntime runtime) {
        this.runtime = runtime;
    }

    public static ZLinkFramework start(Consumer<ZLinkFrameworkOptions> configure) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        configure.accept(options);
        return new ZLinkFramework(
            ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory()));
    }

    public ZLinkClient client() {
        return runtime.client();
    }

    public ZLinkSpotManager spotManager() {
        return runtime.spotManager();
    }

    public ZLinkActorManager actorManager() {
        return runtime.actorManager();
    }

    @Override
    public void close() {
        runtime.close();
    }
}
