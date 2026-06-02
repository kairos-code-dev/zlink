package systems.zlink.framework.runtime.configuration;

import systems.zlink.framework.configuration.ZLinkDiagnosticsOptions;
import systems.zlink.framework.configuration.ZLinkDispatchMode;
import systems.zlink.framework.configuration.ZLinkDispatchOptions;
import systems.zlink.framework.configuration.ZLinkLogLevel;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkDispatchOptionsRegistration implements ZLinkDispatchOptions {
    private final UnhandledDispatchOptions unhandled = new UnhandledDispatchOptions();
    private final DiagnosticsOptions diagnostics = new DiagnosticsOptions();
    private ZLinkDispatchMode spotDispatchMode = ZLinkDispatchMode.COMPILED;
    private ZLinkDispatchMode streamDispatchMode = ZLinkDispatchMode.COMPILED;

    @Override
    public ZLinkDispatchMode spotDispatchMode() {
        return spotDispatchMode;
    }

    @Override
    public void setSpotDispatchMode(ZLinkDispatchMode mode) {
        spotDispatchMode = requireNonNull(mode, "spotDispatchMode");
    }

    @Override
    public ZLinkDispatchMode streamDispatchMode() {
        return streamDispatchMode;
    }

    @Override
    public void setStreamDispatchMode(ZLinkDispatchMode mode) {
        streamDispatchMode = requireNonNull(mode, "streamDispatchMode");
    }

    @Override
    public UnhandledDispatchOptions unhandled() {
        return unhandled;
    }

    @Override
    public DiagnosticsOptions diagnostics() {
        return diagnostics;
    }

    void validate() {
        if (unhandled.send() == ZLinkUnhandledDispatchAction.REPLY_ERROR) {
            throw new ZLinkConfigurationException(
                "unhandled send dispatch cannot use REPLY_ERROR because send has no reply path");
        }
        if (unhandled.publish() == ZLinkUnhandledDispatchAction.REPLY_ERROR) {
            throw new ZLinkConfigurationException(
                "unhandled publish dispatch cannot use REPLY_ERROR because publish has no reply path");
        }
        double sampleRate = diagnostics.sampleRate();
        if (Double.isNaN(sampleRate) || sampleRate < 0.0d || sampleRate > 1.0d) {
            throw new ZLinkConfigurationException(
                "diagnostics sample rate must be between 0.0 and 1.0");
        }
    }

    private static <T> T requireNonNull(T value, String name) {
        if (value == null) {
            throw new ZLinkConfigurationException(name + " is required");
        }
        return value;
    }

    public static final class UnhandledDispatchOptions implements ZLinkUnhandledDispatchOptions {
        private ZLinkUnhandledDispatchAction request = ZLinkUnhandledDispatchAction.REPLY_ERROR;
        private ZLinkUnhandledDispatchAction send = ZLinkUnhandledDispatchAction.LOG_AND_DROP;
        private ZLinkUnhandledDispatchAction publish = ZLinkUnhandledDispatchAction.LOG_AND_DROP;
        private ZLinkLogLevel sendLogLevel = ZLinkLogLevel.WARN;
        private ZLinkLogLevel publishLogLevel = ZLinkLogLevel.DEBUG;

        public ZLinkUnhandledDispatchAction request() {
            return request;
        }

        public ZLinkUnhandledDispatchAction send() {
            return send;
        }

        public ZLinkUnhandledDispatchAction publish() {
            return publish;
        }

        public ZLinkLogLevel sendLogLevel() {
            return sendLogLevel;
        }

        public ZLinkLogLevel publishLogLevel() {
            return publishLogLevel;
        }

        @Override
        public void setRequest(ZLinkUnhandledDispatchAction action) {
            request = requireNonNull(action, "request");
        }

        @Override
        public void setSend(ZLinkUnhandledDispatchAction action) {
            send = requireNonNull(action, "send");
        }

        @Override
        public void setPublish(ZLinkUnhandledDispatchAction action) {
            publish = requireNonNull(action, "publish");
        }

        @Override
        public void setSendLogLevel(ZLinkLogLevel level) {
            sendLogLevel = requireNonNull(level, "sendLogLevel");
        }

        @Override
        public void setPublishLogLevel(ZLinkLogLevel level) {
            publishLogLevel = requireNonNull(level, "publishLogLevel");
        }
    }

    public static final class DiagnosticsOptions implements ZLinkDiagnosticsOptions {
        private ZLinkMessageFlowLogMode messageFlow = ZLinkMessageFlowLogMode.ERRORS_ONLY;
        private double sampleRate = 1.0d;
        private boolean includeMessageSizes = true;
        private boolean includeNativeDiagnostics;

        public ZLinkMessageFlowLogMode messageFlow() {
            return messageFlow;
        }

        public double sampleRate() {
            return sampleRate;
        }

        public boolean includeMessageSizes() {
            return includeMessageSizes;
        }

        public boolean includeNativeDiagnostics() {
            return includeNativeDiagnostics;
        }

        @Override
        public void setMessageFlow(ZLinkMessageFlowLogMode mode) {
            messageFlow = requireNonNull(mode, "messageFlow");
        }

        @Override
        public void setSampleRate(double sampleRate) {
            this.sampleRate = sampleRate;
        }

        @Override
        public void setIncludeMessageSizes(boolean enabled) {
            includeMessageSizes = enabled;
        }

        @Override
        public void setIncludeNativeDiagnostics(boolean enabled) {
            includeNativeDiagnostics = enabled;
        }
    }
}
