namespace Zlink.Framework.Contracts.Dispatch;

using Microsoft.Extensions.Logging;

public interface IZLinkDispatchOptions
{
    ZLinkDispatchMode SpotDispatchMode { get; set; }

    ZLinkDispatchMode StreamDispatchMode { get; set; }

    IZLinkUnhandledDispatchOptions Unhandled { get; }

    IZLinkDiagnosticsOptions Diagnostics { get; }

    IZLinkDispatchOptions SetMessageDispatchErrorObserver<TObserver>()
        where TObserver : class, IZLinkMessageDispatchErrorObserver;

    IZLinkDispatchOptions SetMessageDispatchErrorObserver(
        IZLinkMessageDispatchErrorObserver observer);

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

    // Node identity stamped on every trace line (node=) for cross-node aggregation.
    IZLinkDispatchOptions TraceNodeId(string id);
}

public interface IZLinkMessageDispatchErrorObserver
{
    ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken);
}

public interface IZLinkMessageFlowObserver
{
    ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken);
}

// A success-path transition in a message's lifecycle. Errors are reported
// separately via ZLinkMessageDispatchErrorEvent. received/dispatched/replied are
// inbound (this node receives); sent/reply_received are outbound (this node sends).
public enum ZLinkMessageFlowPhase
{
    Received,
    Dispatched,
    Replied,
    Dropped,
    Sent,
    ReplyReceived
}

public sealed record ZLinkMessageFlowEvent(
    ZLinkMessageFlowPhase Phase,
    ZLinkDispatchErrorSurface Surface,
    ZLinkDispatchMessageKind MessageKind,
    string? PacketName = null,
    string? ChannelName = null,
    string? Topic = null,
    string? CorrelationId = null,
    string? SourceRid = null,
    string? SpotRid = null,
    string? ActorId = null,
    long? MessageSize = null);

public sealed record ZLinkMessageDispatchErrorEvent(
    ZLinkDispatchErrorSurface Surface,
    ZLinkDispatchMessageKind MessageKind,
    ZLinkDispatchErrorReason Reason,
    ZLinkDispatchErrorAction Action,
    string? PacketName,
    string? ChannelName = null,
    string? Topic = null,
    string? SpotRid = null,
    string? ActorId = null,
    string? SourceRid = null,
    string? CorrelationId = null,
    Exception? Exception = null);

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

    // Node/runtime identity stamped on each trace line. Null = omitted.
    string? NodeId { get; }

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
    DealerMeshChannel,
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
