package systems.zlink.framework.spring;

import java.util.Objects;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

public final class ZLinkFrameworkLifecycle implements SmartLifecycle, ZLinkClient {
    private final DefaultZLinkFrameworkOptions options;
    private final ZLinkBackendAdapterFactory backendAdapterFactory;
    private ZLinkFrameworkRuntime runtime;
    private boolean running;

    public ZLinkFrameworkLifecycle(
        DefaultZLinkFrameworkOptions options,
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
        runtime = ZLinkFrameworkRuntime.start(options, backendAdapterFactory);
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
        return 0;
    }

    @Override
    public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
        return requireRuntime().client().sendToChannel(channelName, message);
    }

    @Override
    public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage message) {
        return requireRuntime().client().requestToChannel(channelName, message);
    }

    private synchronized ZLinkFrameworkRuntime requireRuntime() {
        if (!running || runtime == null) {
            throw new ZLinkConfigurationException("ZLink framework runtime is not running");
        }
        return runtime;
    }
}
