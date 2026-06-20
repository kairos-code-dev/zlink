package systems.zlink.framework.runtime.diagnostics;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicLong;
import java.util.logging.Level;
import java.util.logging.Logger;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorEvent;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorObserver;
import systems.zlink.framework.runtime.configuration.ZLinkDispatchOptionsRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

public final class ZLinkDispatchErrorReporter {
    private static final Logger LOGGER =
        Logger.getLogger(ZLinkDispatchErrorReporter.class.getName());

    private final ZLinkDispatchOptionsRegistration options;
    private final ZLinkHandlerFactory handlerFactory;
    private final Executor executor;
    private final AtomicLong reportedCount = new AtomicLong();
    private final AtomicLong observerFailureCount = new AtomicLong();

    public ZLinkDispatchErrorReporter(
        ZLinkDispatchOptionsRegistration options,
        ZLinkHandlerFactory handlerFactory,
        Executor executor) {
        this.options = options;
        this.handlerFactory = handlerFactory;
        this.executor = executor;
    }

    public void report(ZLinkMessageDispatchErrorEvent error) {
        reportedCount.incrementAndGet();
        LOGGER.log(
            Level.SEVERE,
            "ZLink message dispatch error: surface=" + error.surface()
                + ", kind=" + error.messageKind()
                + ", reason=" + error.reason()
                + ", action=" + error.action()
                + ", packet=" + valueOrEmpty(error.packetName())
                + ", channel=" + valueOrEmpty(error.channelName())
                + ", spot=" + valueOrEmpty(error.spotRid()),
            error.exception());
        CompletableFuture.runAsync(() -> {
            try {
                ZLinkMessageDispatchErrorObserver observer = resolveObserver();
                if (observer == null) {
                    return;
                }
                CompletionStage<Void> result = observer.onDispatchError(error);
                if (result != null) {
                    result.exceptionally(ignored -> {
                        observerFailureCount.incrementAndGet();
                        return null;
                    });
                }
            } catch (Throwable ex) {
                observerFailureCount.incrementAndGet();
            }
        }, executor);
    }

    public long reportedCount() {
        return reportedCount.get();
    }

    public long observerFailureCount() {
        return observerFailureCount.get();
    }

    private ZLinkMessageDispatchErrorObserver resolveObserver() {
        if (options.messageDispatchErrorObserver() != null) {
            return options.messageDispatchErrorObserver();
        }
        if (options.messageDispatchErrorObserverType() == null) {
            return null;
        }
        return (ZLinkMessageDispatchErrorObserver) handlerFactory.create(
            options.messageDispatchErrorObserverType());
    }

    private static String valueOrEmpty(String value) {
        return value == null ? "" : value;
    }
}
