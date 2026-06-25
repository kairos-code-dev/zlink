package systems.zlink.framework.runtime.diagnostics;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicLong;
import java.util.logging.Level;
import java.util.logging.Logger;
import systems.zlink.framework.configuration.ZLinkDiagnosticsOptions;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkMessageFlowObserver;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.runtime.configuration.ZLinkDispatchOptionsRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

// Message-flow tracer for success transitions and dispatch errors, keyed by
// correlation id. Mirrors the C++ message_flow_tracer / .NET ZLinkMessageFlowTracer.
//
// PERFORMANCE: callers MUST guard event construction with enabled(outcome) so an "off"
// dispatch pays nothing but a volatile mode read:
//     if (tracer.enabled(outcome)) tracer.trace(new ZLinkMessageFlowEvent(...));
public final class ZLinkMessageFlowTracer {
    private static final Logger LOGGER =
        Logger.getLogger(ZLinkMessageFlowTracer.class.getName());

    private final ZLinkDispatchOptionsRegistration options;
    private final ZLinkHandlerFactory handlerFactory;
    private final Executor executor;
    private final AtomicLong tracedCount = new AtomicLong();
    private final AtomicLong observerFailureCount = new AtomicLong();
    private final AtomicLong sampleCounter = new AtomicLong();

    public ZLinkMessageFlowTracer(
        ZLinkDispatchOptionsRegistration options,
        ZLinkHandlerFactory handlerFactory,
        Executor executor) {
        this.options = options;
        this.handlerFactory = handlerFactory;
        this.executor = executor;
    }

    // Cheap mode gate (volatile read of the live mode). Build the event only after
    // this returns true.
    public boolean enabled(ZLinkMessageFlowOutcome outcome) {
        return options.diagnostics().effectiveMessageFlow().ordinal() >= requiredMode(outcome).ordinal();
    }

    public void trace(ZLinkMessageFlowEvent flow) {
        if (!enabled(flow.outcome())) {
            return;
        }
        if (flow.outcome() != ZLinkMessageFlowOutcome.DROPPED
            && flow.outcome() != ZLinkMessageFlowOutcome.ERROR
            && !sample()) {
            return;
        }
        tracedCount.incrementAndGet();
        try {
            logDefault(flow);
        } catch (Throwable ex) {
            observerFailureCount.incrementAndGet();
        }

        if (options.messageFlowObserver() == null && options.messageFlowObserverType() == null) {
            return;
        }
        CompletableFuture.runAsync(() -> {
            try {
                ZLinkMessageFlowObserver observer = resolveObserver();
                if (observer == null) {
                    return;
                }
                CompletionStage<Void> result = observer.onMessageFlow(flow);
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

    public long tracedCount() {
        return tracedCount.get();
    }

    public long observerFailureCount() {
        return observerFailureCount.get();
    }

    private static ZLinkMessageFlowLogMode requiredMode(ZLinkMessageFlowOutcome outcome) {
        return outcome == ZLinkMessageFlowOutcome.DROPPED
            || outcome == ZLinkMessageFlowOutcome.ERROR
            ? ZLinkMessageFlowLogMode.ERRORS_ONLY
            : ZLinkMessageFlowLogMode.KEY_TRANSITIONS;
    }

    private boolean sample() {
        double rate = options.diagnostics().sampleRate();
        if (rate >= 1.0d) {
            return true;
        }
        if (rate <= 0.0d) {
            return false;
        }
        long stride = Math.max(1L, Math.round(1.0d / rate));
        return sampleCounter.incrementAndGet() % stride == 0;
    }

    private ZLinkMessageFlowObserver resolveObserver() {
        if (options.messageFlowObserver() != null) {
            return options.messageFlowObserver();
        }
        if (options.messageFlowObserverType() == null) {
            return null;
        }
        return (ZLinkMessageFlowObserver) handlerFactory.create(options.messageFlowObserverType());
    }

    private void logDefault(ZLinkMessageFlowEvent flow) {
        ZLinkDiagnosticsOptions diagnostics = options.diagnostics();
        Long size = null;
        if (flow.messageSize() != null
            && diagnostics.effectiveMessageFlow().ordinal() >= ZLinkMessageFlowLogMode.VERBOSE.ordinal()
            && diagnostics.includeMessageSizes()) {
            size = flow.messageSize();
        }

        String line = ZLinkTraceFormat.flowLine(flow, diagnostics.label(), size);
        // Separated file (diagnostics.logFile) vs the shared app logger. Both carry
        // structured key=value tokens (greppable by corr); the observer gets the
        // typed event for collector ingest.
        if (diagnostics.logFile() != null) {
            ZLinkTraceFileWriter.write(diagnostics.logFile(), line);
        } else {
            LOGGER.log(Level.INFO, line);
        }
    }
}
