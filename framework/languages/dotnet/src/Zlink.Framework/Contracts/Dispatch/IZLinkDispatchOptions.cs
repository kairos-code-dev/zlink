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
}

public interface IZLinkMessageDispatchErrorObserver
{
    ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken);
}

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

public interface IZLinkDiagnosticsOptions
{
    ZLinkMessageFlowLogMode MessageFlow { get; set; }

    double SampleRate { get; set; }

    bool IncludeMessageSizes { get; set; }

    bool IncludeNativeDiagnostics { get; set; }
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
