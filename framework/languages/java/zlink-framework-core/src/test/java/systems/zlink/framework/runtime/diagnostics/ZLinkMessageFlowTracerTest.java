package systems.zlink.framework.runtime.diagnostics;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkMessageFlowPhase;
import systems.zlink.framework.runtime.configuration.ZLinkDispatchOptionsRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

// MFLOW-001/002/003/011: mode gating + runtime live toggle + structured output.
class ZLinkMessageFlowTracerTest {
    private static ZLinkDispatchOptionsRegistration options(ZLinkMessageFlowLogMode mode) {
        ZLinkDispatchOptionsRegistration options = new ZLinkDispatchOptionsRegistration();
        options.messageFlow(mode);
        return options;
    }

    private static ZLinkMessageFlowTracer tracer(ZLinkDispatchOptionsRegistration options) {
        return new ZLinkMessageFlowTracer(options, ZLinkHandlerFactory.reflection(), Runnable::run);
    }

    private static ZLinkMessageFlowEvent flow(ZLinkMessageFlowPhase phase) {
        return new ZLinkMessageFlowEvent(
            phase,
            ZLinkDispatchErrorSurface.CHANNEL,
            ZLinkDispatchMessageKind.REQUEST,
            "PlaceOrder", "orders", null, "corr-1", null, null, null, null);
    }

    @Test
    void offSuppressesAllTransitions() {
        ZLinkMessageFlowTracer tracer = tracer(options(ZLinkMessageFlowLogMode.OFF));
        assertFalse(tracer.enabled(ZLinkMessageFlowPhase.RECEIVED));
        assertFalse(tracer.enabled(ZLinkMessageFlowPhase.DROPPED));
    }

    @Test
    void errorsOnlyEmitsDroppedNotReceived() {
        ZLinkMessageFlowTracer tracer = tracer(options(ZLinkMessageFlowLogMode.ERRORS_ONLY));
        assertFalse(tracer.enabled(ZLinkMessageFlowPhase.RECEIVED));
        assertTrue(tracer.enabled(ZLinkMessageFlowPhase.DROPPED));
    }

    @Test
    void keyTransitionsEmitsLifecycle() {
        ZLinkMessageFlowTracer tracer = tracer(options(ZLinkMessageFlowLogMode.KEY_TRANSITIONS));
        assertTrue(tracer.enabled(ZLinkMessageFlowPhase.RECEIVED));
        assertTrue(tracer.enabled(ZLinkMessageFlowPhase.REPLIED));
    }

    @Test
    void liveModeOverridesAndTogglesAtRuntime() {
        ZLinkDispatchOptionsRegistration options = options(ZLinkMessageFlowLogMode.OFF);
        AtomicReference<ZLinkMessageFlowLogMode> cell =
            new AtomicReference<>(ZLinkMessageFlowLogMode.OFF);
        options.diagnostics().installLiveMode(cell);
        ZLinkMessageFlowTracer tracer = tracer(options);

        assertFalse(tracer.enabled(ZLinkMessageFlowPhase.RECEIVED));
        cell.set(ZLinkMessageFlowLogMode.KEY_TRANSITIONS);
        assertTrue(tracer.enabled(ZLinkMessageFlowPhase.RECEIVED));
        cell.set(ZLinkMessageFlowLogMode.OFF);
        assertFalse(tracer.enabled(ZLinkMessageFlowPhase.RECEIVED));
    }

    @Test
    void writesStructuredLineToSeparatedFile() throws Exception {
        Path file = Files.createTempFile("zlink-flow", ".log");
        Files.deleteIfExists(file);
        ZLinkDispatchOptionsRegistration options = options(ZLinkMessageFlowLogMode.KEY_TRANSITIONS);
        options.traceLogFile(file.toString()).traceNodeId("api");
        ZLinkMessageFlowTracer tracer = tracer(options);

        tracer.trace(flow(ZLinkMessageFlowPhase.RECEIVED));
        tracer.trace(flow(ZLinkMessageFlowPhase.REPLIED));

        String content = Files.readString(file);
        assertTrue(content.contains("phase=RECEIVED"), content);
        assertTrue(content.contains("phase=REPLIED"), content);
        assertTrue(content.contains("corr=corr-1"), content);
        assertTrue(content.contains("node=api"), content);
        Files.deleteIfExists(file);
        assertEquals(2L, tracer.tracedCount());
    }
}
