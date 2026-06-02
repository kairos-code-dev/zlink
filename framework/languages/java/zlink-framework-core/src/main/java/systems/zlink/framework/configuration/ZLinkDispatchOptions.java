package systems.zlink.framework.configuration;

public interface ZLinkDispatchOptions {
    ZLinkDispatchMode spotDispatchMode();

    void setSpotDispatchMode(ZLinkDispatchMode mode);

    ZLinkDispatchMode streamDispatchMode();

    void setStreamDispatchMode(ZLinkDispatchMode mode);

    ZLinkUnhandledDispatchOptions unhandled();

    ZLinkDiagnosticsOptions diagnostics();
}
