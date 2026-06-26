namespace Zlink.Framework.Contracts.Dispatch;

using Microsoft.Extensions.Logging;

public interface IZLinkDispatchOptions
{
    ZLinkDispatchMode SpotDispatchMode { get; set; }

    ZLinkDispatchMode StreamDispatchMode { get; set; }

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
    void SetMessageFlowMode(ZLinkMessageFlowLogMode mode);

    ZLinkMessageFlowLogMode MessageFlowMode { get; }
}

// A transition or error result in a message's lifecycle.
public enum ZLinkMessageFlowOutcome
{
    Received,
    Dispatched,
    Replied,
    Dropped,
    Sent,
    ReplyReceived,
    Error
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
    string? SpotRid = null,
    string? ActorId = null,
    long? MessageSize = null,
    ZLinkDispatchErrorReason? ErrorReason = null,
    ZLinkDispatchErrorAction? ErrorAction = null,
    string? ErrorType = null,
    string? ErrorMessage = null);

public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }

    ZLinkUnhandledDispatchAction Send { get; set; }

    ZLinkUnhandledDispatchAction Publish { get; set; }

    LogLevel SendLogLevel { get; set; }

    LogLevel PublishLogLevel { get; set; }
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
    ReplyError,
    LogAndDrop,
    Drop,
    Throw
}

public enum ZLinkMessageFlowLogMode
{
    Off,
    ErrorsOnly,
    KeyTransitions,
    Verbose,
    Diagnostic
}

public enum ZLinkDispatchErrorSurface
{
    Channel,
    RouteMeshChannel,
    SpotRoute,
    SpotSubscription,
    SpotActor,
    StreamSession
}

public enum ZLinkDispatchMessageKind
{
    Request,
    Send,
    Publish,
    Response,
    Error,
    ActorRequest,
    ActorSend
}

public enum ZLinkDispatchErrorReason
{
    HandlerMissing,
    PayloadDecodeFailed,
    HandlerException,
    InvalidFrame,
    ReplyPathMissing,
    UnexpectedReply
}

public enum ZLinkDispatchErrorAction
{
    ReplyError,
    Drop
}
