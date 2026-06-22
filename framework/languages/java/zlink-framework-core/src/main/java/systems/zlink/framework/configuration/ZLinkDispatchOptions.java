package systems.zlink.framework.configuration;

public interface ZLinkDispatchOptions {
    ZLinkDispatchMode spotDispatchMode();

    void setSpotDispatchMode(ZLinkDispatchMode mode);

    ZLinkDispatchMode streamDispatchMode();

    void setStreamDispatchMode(ZLinkDispatchMode mode);

    ZLinkUnhandledDispatchOptions unhandled();

    ZLinkDiagnosticsOptions diagnostics();

    ZLinkDispatchOptions setMessageDispatchErrorObserver(
        Class<? extends ZLinkMessageDispatchErrorObserver> observerType);

    ZLinkDispatchOptions setMessageDispatchErrorObserver(
        ZLinkMessageDispatchErrorObserver observer);

    ZLinkDispatchOptions setMessageFlowObserver(
        Class<? extends ZLinkMessageFlowObserver> observerType);

    ZLinkDispatchOptions setMessageFlowObserver(
        ZLinkMessageFlowObserver observer);

    // Fluent diagnostics/tracing config (builder-chain only; the diagnostics fields
    // are read-only, configure them through these).
    ZLinkDispatchOptions messageFlow(ZLinkMessageFlowLogMode mode);

    ZLinkDispatchOptions traceSampleRate(double rate);

    ZLinkDispatchOptions includeMessageSizes(boolean include);

    // Send tracing/error logs to a dedicated file (separated from app logs).
    ZLinkDispatchOptions traceLogFile(String path);

    // Node identity stamped on every trace line (node=) for cross-node aggregation.
    ZLinkDispatchOptions traceNodeId(String id);
}
