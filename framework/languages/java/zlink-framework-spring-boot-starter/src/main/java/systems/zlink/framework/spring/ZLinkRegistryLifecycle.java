package systems.zlink.framework.spring;

import java.util.Objects;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;

public final class ZLinkRegistryLifecycle implements SmartLifecycle {
    public static final int PHASE = -100;

    private final ZLinkEmbeddedRegistryOptions options;
    private final ZLinkBackendAdapterFactory backendAdapterFactory;
    private ZLinkRegistryRuntime runtime;
    private boolean running;

    public ZLinkRegistryLifecycle(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendAdapterFactory) {
        this.options = Objects.requireNonNull(options, "options");
        this.backendAdapterFactory = Objects.requireNonNull(
            backendAdapterFactory,
            "backendAdapterFactory");
    }

    @Override
    public synchronized void start() {
        if (running) {
            return;
        }
        runtime = ZLinkRegistryRuntime.start(options, backendAdapterFactory);
        running = true;
    }

    @Override
    public synchronized void stop() {
        if (!running) {
            return;
        }
        try {
            runtime.close();
        } finally {
            runtime = null;
            running = false;
        }
    }

    @Override
    public void stop(Runnable callback) {
        try {
            stop();
        } finally {
            callback.run();
        }
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    @Override
    public boolean isAutoStartup() {
        return true;
    }

    @Override
    public int getPhase() {
        return PHASE;
    }
}
