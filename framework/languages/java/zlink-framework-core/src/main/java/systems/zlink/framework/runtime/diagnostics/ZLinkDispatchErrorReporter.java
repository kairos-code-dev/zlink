package systems.zlink.framework.runtime.diagnostics;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicLong;
import java.util.logging.Level;
import java.util.logging.Logger;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorEvent;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorObserver;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
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
    // Success-path tracer companion: every surface already receives a reporter, so
    // exposing the flow tracer here wires all dispatch sites without threading a new
    // parameter. Shares the same options (live mode), factory and executor.
    private final ZLinkMessageFlowTracer flow;

    public ZLinkDispatchErrorReporter(
        ZLinkDispatchOptionsRegistration options,
        ZLinkHandlerFactory handlerFactory,
        Executor executor) {
        this.options = options;
        this.handlerFactory = handlerFactory;
        this.executor = executor;
        this.flow = new ZLinkMessageFlowTracer(options, handlerFactory, executor);
    }

    public ZLinkMessageFlowTracer flow() {
        return flow;
    }

    public void report(ZLinkMessageDispatchErrorEvent error) {
        reportedCount.incrementAndGet();
        // off silences the default error log; every other mode keeps reporting errors
        // (errors_only is the default). A registered observer still fires.
        if (options.diagnostics().effectiveMessageFlow() != ZLinkMessageFlowLogMode.OFF) {
            logDefault(error);
        }
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

    private void logDefault(ZLinkMessageDispatchErrorEvent error) {
        String nodeId = options.diagnostics().nodeId();
        // Separated tracing file vs the shared app logger (same choice as the flow
        // tracer); format unified (corr/topic/src/actor/node) so a single grep on
        // corr follows a message whether it succeeded or failed.
        String line = ZLinkTraceFormat.errorLine(error, nodeId);
        if (options.diagnostics().logFile() != null) {
            ZLinkTraceFileWriter.write(options.diagnostics().logFile(), line);
        } else {
            LOGGER.log(Level.SEVERE, line, error.exception());
        }
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
}
