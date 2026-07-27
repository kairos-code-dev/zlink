package systems.zlink.framework.spring;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.core.ResolvableType;
import org.springframework.context.SmartLifecycle;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRuntimeEvent;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.internal.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.runtime.monitoring.ZLinkMonitoringRuntime;

public final class ZLinkMonitoringLifecycle implements SmartLifecycle {
    public static final int PHASE = ZLinkFrameworkLifecycle.PHASE + 100;

    private final DefaultZLinkMonitoringOptions options;
    private final ZLinkBackendAdapterProvider backendAdapterFactory;
    private final ZLinkRuntimeEventDispatcher dispatcher;
    private final ObjectProvider<ZLinkFrameworkLifecycle> frameworkLifecycle;
    private final List<ZLinkRuntimeEventHandler<?>> eventHandlers;
    private final List<AutoCloseable> handlerRegistrations = new ArrayList<>();
    private ScheduledExecutorService poller;
    private ZLinkMonitoringRuntime runtime;
    private boolean running;

    public ZLinkMonitoringLifecycle(
        DefaultZLinkMonitoringOptions options,
        ZLinkBackendAdapterProvider backendAdapterFactory,
        ZLinkRuntimeEventDispatcher dispatcher,
        ObjectProvider<ZLinkFrameworkLifecycle> frameworkLifecycle,
        List<ZLinkRuntimeEventHandler<?>> eventHandlers) {
        this.options = Objects.requireNonNull(options, "options");
        this.backendAdapterFactory = Objects.requireNonNull(
            backendAdapterFactory,
            "backendAdapterFactory");
        this.dispatcher = Objects.requireNonNull(dispatcher, "dispatcher");
        this.frameworkLifecycle = Objects.requireNonNull(
            frameworkLifecycle,
            "frameworkLifecycle");
        this.eventHandlers = List.copyOf(eventHandlers);
    }

    @Override
    public synchronized void start() {
        if (running) {
            return;
        }
        try {
            if (!options.hasSources()) {
                registerEventHandlers();
                running = true;
                return;
            }
            ZLinkFrameworkLifecycle framework = frameworkLifecycle.getIfAvailable();
            boolean needsFramework = !options.socketSourceNames().isEmpty()
                || !options.spotSourceNames().isEmpty()
                || !options.locationRuntimeSourceNames().isEmpty();
            if (needsFramework && framework != null && !framework.isRunning()) {
                framework.start();
            }
            ZLinkMonitoringBackendAdapter backend =
                backendAdapterFactory.createMonitoringAdapter(
                    new ZLinkBackendAdapterOptions(Duration.ofSeconds(30)));
            runtime = new ZLinkMonitoringRuntime(
                options,
                backend,
                !needsFramework || framework == null
                    ? Map.of()
                    : framework.monitoringSocketSources(),
                !needsFramework || framework == null
                    ? Map.of()
                    : framework.monitoringSpotSources(),
                !needsFramework || framework == null || options.locationRuntimeSourceNames().isEmpty()
                    ? null
                    : framework.monitoringLocationRuntimeQuery(),
                dispatcher);
            registerEventHandlers();
            startPolling();
            running = true;
        } catch (RuntimeException ex) {
            stopAfterFailedStart();
            throw ex;
        }
    }

    @Override
    public synchronized void stop() {
        if (!running) {
            return;
        }
        try {
            teardown();
        } finally {
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

    private void startPolling() {
        poller = Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-java-monitoring");
            thread.setDaemon(true);
            return thread;
        });
        long intervalMs = Math.max(1, options.pollInterval().toMillis());
        poller.scheduleWithFixedDelay(
            runtime::pollSnapshots,
            0,
            intervalMs,
            TimeUnit.MILLISECONDS);
    }

    private void registerEventHandlers() {
        for (ZLinkRuntimeEventHandler<?> handler : eventHandlers) {
            handlerRegistrations.add(registerEventHandler(handler));
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private AutoCloseable registerEventHandler(ZLinkRuntimeEventHandler<?> handler) {
        Class<?> eventType = ResolvableType.forClass(handler.getClass())
            .as(ZLinkRuntimeEventHandler.class)
            .getGeneric(0)
            .resolve();
        if (eventType == null || !ZLinkRuntimeEvent.class.isAssignableFrom(eventType)) {
            throw new ZLinkConfigurationException(
                "runtime event handler event type is required: " + handler.getClass().getName());
        }
        return dispatcher.register(
            (Class<? extends ZLinkRuntimeEvent>) eventType,
            (ZLinkRuntimeEventHandler) handler);
    }

    private static void closeQuietly(AutoCloseable closeable) {
        try {
            closeable.close();
        } catch (Exception ignored) {
        }
    }

    private void stopAfterFailedStart() {
        teardown();
        running = false;
    }

    private void teardown() {
        if (poller != null) {
            poller.shutdownNow();
            poller = null;
        }
        for (AutoCloseable registration : List.copyOf(handlerRegistrations)) {
            closeQuietly(registration);
        }
        handlerRegistrations.clear();
        if (runtime != null) {
            runtime.close();
            runtime = null;
        }
    }
}
