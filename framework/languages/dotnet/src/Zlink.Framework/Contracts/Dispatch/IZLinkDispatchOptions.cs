namespace Zlink.Framework.Contracts.Dispatch;

public interface IZLinkDispatchOptions
{
    IZLinkUnhandledDispatchOptions Unhandled { get; }

    IZLinkDiagnosticsOptions Diagnostics { get; }

    // Fluent diagnostics/tracing config (builder-chain only; the diagnostics fields
    // are read-only, configure them through these).
    IZLinkDispatchOptions TraceSampleRate(double rate);

    IZLinkDispatchOptions IncludeMessageSizes(bool include);

    // Send tracing/error logs to a dedicated file (separated from app logs).
    IZLinkDispatchOptions TraceLogFile(string path);

    // Human-readable trace label stamped on trace lines as label=.
    IZLinkDispatchOptions TraceLabel(string label);

    IZLinkDispatchOptions SetRuntimeMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkRuntimeMessageFlowObserver;

    IZLinkDispatchOptions SetRuntimeMessageFlowObserver(
        IZLinkRuntimeMessageFlowObserver observer);

    IZLinkDispatchOptions SetRuntimeErrorSink<TSink>()
        where TSink : class, IZLinkRuntimeErrorSink;

    IZLinkDispatchOptions SetRuntimeErrorSink(IZLinkRuntimeErrorSink sink);

    IZLinkDispatchOptions MessageFlow(ZLinkRuntimeMessageFlowMode mode);
}

public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }

    ZLinkUnhandledDispatchAction Send { get; set; }

    ZLinkUnhandledDispatchAction Publish { get; set; }
}

// Read-only diagnostics view. Configure via the fluent builder on
// IZLinkDispatchOptions (ConfigureDispatch().MessageFlow(...).TraceLogFile(...)...).
public interface IZLinkDiagnosticsOptions
{
    // Configured (static) mode. Use EffectiveMessageFlow for runtime decisions.
    ZLinkRuntimeMessageFlowMode MessageFlow { get; }

    double SampleRate { get; }

    bool IncludeMessageSizes { get; }

    // When set, tracing/error logs go to this dedicated file (separated from app
    // logs). Null = shared app logger.
    string? LogFile { get; }

    // Human-readable runtime label stamped on trace lines. Null = omitted.
    string? Label { get; }

    // The mode actually in effect: the runtime-mutable live override if installed,
    // else the configured mode (read live on every dispatch).
    ZLinkRuntimeMessageFlowMode EffectiveMessageFlow { get; }
}

public enum ZLinkUnhandledDispatchAction
{
    ReplyError = 0,
    LogAndDrop = 1,
    Drop = 2,
    Throw = 3
}
