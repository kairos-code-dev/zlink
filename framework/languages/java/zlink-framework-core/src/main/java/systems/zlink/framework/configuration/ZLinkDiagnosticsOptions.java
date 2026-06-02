package systems.zlink.framework.configuration;

public interface ZLinkDiagnosticsOptions {
    void setMessageFlow(ZLinkMessageFlowLogMode mode);

    void setSampleRate(double sampleRate);

    void setIncludeMessageSizes(boolean enabled);

    void setIncludeNativeDiagnostics(boolean enabled);
}
