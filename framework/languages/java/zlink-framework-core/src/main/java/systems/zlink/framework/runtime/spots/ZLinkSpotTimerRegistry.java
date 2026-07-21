package systems.zlink.framework.runtime.spots;

import java.lang.reflect.InvocationTargetException;
import java.time.Duration;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerOverrunPolicy;
import systems.zlink.framework.spots.ZLinkTimerTick;

final class ZLinkSpotTimerRegistry implements AutoCloseable {
    private final RoutingId spotRid;
    private final ScheduledExecutorService executor;
    private final ZLinkHandlerActivator handlerFactory;
    private final List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers;
    private final ZLinkRuntimeEventDispatcher eventDispatcher;
    private final String sourceName;
    private final Dispatch dispatch;
    private final Map<String, ManagedTimer> timers = new LinkedHashMap<>();
    private ZLinkSpot<?> spot;

    ZLinkSpotTimerRegistry(
        RoutingId spotRid,
        ScheduledExecutorService executor,
        ZLinkHandlerActivator handlerFactory,
        List<ZLinkSuspendInvocationAdapter> suspendHandlerInvokers,
        ZLinkRuntimeEventDispatcher eventDispatcher,
        String sourceName,
        Dispatch dispatch) {
        this.spotRid = spotRid;
        this.executor = executor;
        this.handlerFactory = handlerFactory;
        this.suspendHandlerInvokers = suspendHandlerInvokers;
        this.eventDispatcher = eventDispatcher;
        this.sourceName = sourceName;
        this.dispatch = dispatch;
    }

    void setSpot(ZLinkSpot<?> spot) {
        this.spot = spot;
    }

    synchronized CompletionStage<ZLinkTimer> add(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options) {
        if (name == null || name.isBlank()) {
            throw new ZLinkConfigurationException("timer name is required");
        }
        if (period == null || period.isNegative() || period.isZero()) {
            throw new ZLinkConfigurationException("timer period must be positive");
        }
        ZLinkTimerOptions timerOptions = options == null
            ? new ZLinkTimerOptions(
                ZLinkTimerOverrunPolicy.SKIP_LATE_TICKS,
                1,
                true)
            : options;
        if (timerOptions.overrunPolicy() == null) {
            throw new ZLinkConfigurationException("timer overrun policy is required");
        }
        if (timerOptions.overrunPolicy() == ZLinkTimerOverrunPolicy.CATCH_UP_BOUNDED
            && timerOptions.maxCatchUpTicks() <= 0) {
            throw new ZLinkConfigurationException(
                "timer maxCatchUpTicks must be greater than zero");
        }
        ManagedTimer timer = new ManagedTimer(name, period, handlerType, timerOptions);
        ManagedTimer previous = timers.put(name, timer);
        if (previous != null) {
            previous.close();
        }
        timer.start();
        return CompletableFuture.completedFuture(timer);
    }

    @Override
    public synchronized void close() {
        timers.values().forEach(ManagedTimer::close);
        timers.clear();
    }

    private synchronized void removeTimer(String name, ManagedTimer timer) {
        timers.remove(name, timer);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> invokeHandler(Class<?> handlerType, ZLinkTimerTick tick) {
        try {
            Object handler = handlerFactory.create(handlerType);
            return ZLinkHandlerMethodInvoker
                .invokeHandler(
                    handler,
                    "handle",
                    new Object[] {spot, tick},
                    suspendHandlerInvokers)
                .thenApply(ignored -> null);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "failed to create timer handler: " + handlerType.getName(),
                ex));
        }
    }

    private void publishFailure(
        ManagedTimer timer,
        ZLinkTimerTick tick,
        Throwable error,
        boolean stopped) {
        if (eventDispatcher == null) {
            return;
        }
        Throwable failure = unwrap(error);
        eventDispatcher.publish(new ZLinkSpotEvent(
            sourceName,
            Instant.now(),
            stopped
                ? ZLinkSpotEventKind.TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION
                : ZLinkSpotEventKind.TIMER_HANDLER_FAILED,
            Optional.empty(),
            List.of(),
            List.of(),
            Optional.of("spotRid=" + spotRid
                + "|timer=" + timer.name
                + "|handler=" + timer.handlerType.getName()
                + "|deliveryIndex=" + tick.deliveryIndex()
                + "|scheduledIndex=" + tick.scheduledIndex()
                + "|exception=" + failure.getClass().getName()
                + "|message=" + String.valueOf(failure.getMessage()))));
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException
            || current instanceof InvocationTargetException)
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private final class ManagedTimer implements ZLinkTimer {
        private final String name;
        private final Class<?> handlerType;
        private final ZLinkTimerOptions options;
        private final ZLinkSpotTimerSchedule schedule;
        private volatile boolean disposed;
        private ScheduledFuture<?> future;

        ManagedTimer(
            String name,
            Duration period,
            Class<?> handlerType,
            ZLinkTimerOptions options) {
            this.name = name;
            this.handlerType = handlerType;
            this.options = options;
            this.schedule = new ZLinkSpotTimerSchedule(name, period, options);
        }

        void start() {
            scheduleNext(schedule.initialDelayNanos());
        }

        private void scheduleNext(long delayNanos) {
            if (disposed) {
                return;
            }
            future = executor.schedule(
                this::run,
                Math.max(0L, delayNanos),
                TimeUnit.NANOSECONDS);
        }

        private void run() {
            if (disposed) {
                return;
            }
            ZLinkSpotTimerSchedule.PendingTick pendingTick =
                schedule.nextTick(schedule.startedElapsedNanos(), Instant.now());
            ZLinkTimerTick tick = pendingTick.tick();
            dispatch.enqueue(() -> disposed
                    ? CompletableFuture.completedFuture(null)
                    : invokeHandler(handlerType, tick))
                .whenComplete((ignored, error) -> {
                    boolean stopped = false;
                    if (error == null) {
                        schedule.markDelivered(pendingTick);
                    } else {
                        stopped = options.stopOnUnhandledException();
                        if (stopped) {
                            close();
                        }
                        publishFailure(this, tick, error, stopped);
                    }
                    if (!stopped) {
                        scheduleAfterDispatch();
                    }
                });
        }

        private void scheduleAfterDispatch() {
            if (!disposed) {
                scheduleNext(schedule.delayAfterDispatchNanos());
            }
        }

        @Override
        public boolean isDisposed() {
            return disposed;
        }

        @Override
        public CompletionStage<Void> cancel() {
            close();
            removeTimer(name, this);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }

        @Override
        public void close() {
            disposed = true;
            if (future != null) {
                future.cancel(false);
            }
        }
    }

    @FunctionalInterface
    interface Dispatch {
        CompletionStage<Void> enqueue(Supplier<CompletionStage<Void>> operation);
    }
}
