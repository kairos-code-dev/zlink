package systems.zlink.framework.runtime.channels;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.BooleanSupplier;
import java.util.function.Consumer;
import java.util.function.Supplier;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSubscriberSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendTopicMessage;

final class ZLinkChannelReceiveLoops implements AutoCloseable {
    private final BooleanSupplier running;
    private final ScheduledExecutorService executor =
        Executors.newScheduledThreadPool(2, task -> {
            Thread thread = new Thread(task, "zlink-java-channel-runtime");
            thread.setDaemon(true);
            return thread;
        });

    ZLinkChannelReceiveLoops(BooleanSupplier running) {
        this.running = running;
    }

    void startRequest(
        ZLinkBackendRouterSocket router,
        Consumer<ZLinkBackendReceived> dispatch,
        Consumer<Throwable> reportFailure) {
        start(new ReceiveLoop(reportFailure) {
            @Override
            boolean receiveAndDispatch() {
                ZLinkBackendReceived received = router.recv(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return false;
                }
                dispatch.accept(received);
                return true;
            }
        });
    }

    void startSubscribe(
        ZLinkBackendSubscriberSocket subscriber,
        Consumer<ZLinkBackendTopicMessage> dispatch,
        Consumer<Throwable> reportFailure) {
        start(new ReceiveLoop(reportFailure) {
            @Override
            boolean receiveAndDispatch() {
                ZLinkBackendTopicMessage received =
                    subscriber.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    return false;
                }
                dispatch.accept(received);
                return true;
            }
        });
    }

    void startRoute(
        ZLinkBackendRouterSocket router,
        Supplier<Object> socketLock,
        Runnable drainBridge,
        Consumer<ZLinkBackendReceived> dispatch,
        Consumer<Throwable> reportFailure) {
        start(new ReceiveLoop(reportFailure) {
            @Override
            boolean receiveAndDispatch() {
                drainBridge.run();
                ZLinkBackendReceived received;
                synchronized (socketLock.get()) {
                    received = router.recv(ZLinkBackendRecvMode.DONT_WAIT);
                }
                if (received == null) {
                    drainBridge.run();
                    return false;
                }
                dispatch.accept(received);
                return true;
            }
        });
    }

    @Override
    public void close() {
        executor.shutdown();
    }

    void awaitTermination() {
        ZLinkChannelRuntime.awaitTerminated(executor);
    }

    private void start(ReceiveLoop loop) {
        executor.execute(loop);
    }

    private abstract class ReceiveLoop implements Runnable {
        private final Consumer<Throwable> reportFailure;
        private final NoDataBackoff backoff = new NoDataBackoff();

        private ReceiveLoop(Consumer<Throwable> reportFailure) {
            this.reportFailure = reportFailure;
        }

        @Override
        public final void run() {
            if (!running.getAsBoolean()) {
                return;
            }
            try {
                if (receiveAndDispatch()) {
                    backoff.reset();
                    executor.execute(this);
                } else {
                    scheduleAfterNoData();
                }
            } catch (RuntimeException error) {
                if (ZLinkChannelRuntime.isNoDataReceive(error)) {
                    scheduleAfterNoData();
                } else {
                    reportFailure.accept(error);
                    executor.execute(this);
                }
            }
        }

        abstract boolean receiveAndDispatch();

        private void scheduleAfterNoData() {
            executor.schedule(
                this,
                backoff.nextDelayNanos(),
                TimeUnit.NANOSECONDS);
        }
    }
}
