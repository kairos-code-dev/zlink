namespace Zlink.Framework.Contracts.Dispatch;

public interface IZLinkDispatchOptions
{
    IZLinkUnhandledDispatchOptions Unhandled { get; }

    IZLinkDiagnosticsOptions Diagnostics { get; }

    IZLinkDispatchOptions SetMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkMessageFlowObserver;

    IZLinkDispatchOptions SetMessageFlowObserver(IZLinkMessageFlowObserver observer);

    // Fluent diagnostics/tracing config (builder-chain only; the diagnostics fields
    // are read-only, configure them through these).
    IZLinkDispatchOptions MessageFlow(ZLinkMessageFlowLogMode mode);

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

public interface IZLinkMessageFlowObserver
{
    ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken);
}

// Resolve from DI to turn message-flow tracing on/off (or change verbosity) at
// runtime — for temporary diagnostics in production without a restart. The change
// is read live by every dispatch surface. Thread-safe.
public interface IZLinkMessageFlowControl
{
    ZLinkMessageFlowLogMode MessageFlowMode { get; }
    void SetMessageFlowMode(ZLinkMessageFlowLogMode mode);
}

// A transition or error result in a message's lifecycle.
public enum ZLinkMessageFlowOutcome
{
    Received = 0,
    Dispatched = 1,
    Replied = 2,
    Dropped = 3,
    Sent = 4,
    ReplyReceived = 5,
    Error = 6
}

public sealed record ZLinkMessageFlowEvent(
    ZLinkMessageFlowOutcome Outcome,
    ZLinkDispatchErrorSurface Surface,
    ZLinkDispatchMessageKind MessageKind,
    string? PacketName = null,
    string? ChannelName = null,
    string? Topic = null,
    string? CorrelationId = null,
    string? SourceRid = null,
    string? LocalRid = null,
    string? PeerRid = null,
    string? SocketRole = null,
    string? SpotId = null,
    string? ActorId = null,
    long? MessageSize = null,
    ZLinkDispatchErrorReason? ErrorReason = null,
    ZLinkDispatchErrorAction? ErrorAction = null,
    string? ErrorType = null,
    string? ErrorMessage = null)
{
    public string FlowId { get; init; } = string.Empty;

    public ZLinkFlowOrigin? FlowOrigin { get; init; }
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
    ZLinkMessageFlowLogMode MessageFlow { get; }

    double SampleRate { get; }

    bool IncludeMessageSizes { get; }

    bool IncludeNativeDiagnostics { get; }

    // When set, tracing/error logs go to this dedicated file (separated from app
    // logs). Null = shared app logger.
    string? LogFile { get; }

    // Human-readable runtime label stamped on trace lines. Null = omitted.
    string? Label { get; }

    // The mode actually in effect: the runtime-mutable live override if installed,
    // else the configured mode (read live on every dispatch).
    ZLinkMessageFlowLogMode EffectiveMessageFlow { get; }
}

public enum ZLinkUnhandledDispatchAction
{
    ReplyError = 0,
    LogAndDrop = 1,
    Drop = 2,
    Throw = 3
}

public enum ZLinkMessageFlowLogMode
{
    Off = 0,
    ErrorsOnly = 1,
    KeyTransitions = 2,
    Verbose = 3,
    Diagnostic = 4
}

public enum ZLinkDispatchErrorSurface
{
    Channel = 0,
    RouteMeshChannel = 1,
    SpotRoute = 2,
    SpotSubscription = 3,
    SpotActor = 4,
    StreamSession = 5
}

public enum ZLinkDispatchMessageKind
{
    Request = 0,
    Send = 1,
    Publish = 2,
    Response = 3,
    Error = 4,
    ActorRequest = 5,
    ActorSend = 6
}

public enum ZLinkDispatchErrorReason
{
    HandlerMissing = 0,
    PayloadDecodeFailed = 1,
    HandlerException = 2,
    InvalidFrame = 3,
    ReplyPathMissing = 4,
    UnexpectedReply = 5
}

public enum ZLinkDispatchErrorAction
{
    ReplyError = 0,
    Drop = 1,
    FailCaller = 2
}
