using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Diagnostics;

internal readonly struct ZLinkDispatchFlowScope(
    ZLinkDispatchErrorSurface surface,
    string surfaceName,
    ZLinkDispatchMessageKind messageKind,
    string kindName,
    string packetName,
    string? channelName = null,
    string? contentType = null,
    string? correlationId = null,
    string? topic = null,
    string? sourceRid = null,
    string? spotRid = null,
    string? actorId = null,
    string? actorType = null,
    string? logSpotRid = null)
{
    public string? ChannelName => channelName;

    public string? ContentType => contentType;

    public string PacketName => packetName;

    public bool TryDecode(
        IReadOnlyList<Message> parts,
        Type messageType,
        string contentType,
        ZLinkCodecRegistryBuilder codecs,
        ILogger logger,
        ZLinkDispatchErrorReporter dispatchErrors,
        ZLinkDispatchErrorAction failureAction,
        out object? message,
        Func<Exception, Exception>? reportException = null)
    {
        try
        {
            message = ZLinkEnvelopeCodec.DecodeBody(
                parts,
                messageType,
                contentType,
                codecs);
            return true;
        }
        catch (Exception ex)
        {
            message = null;
            PayloadDecodeFailed(
                logger,
                dispatchErrors,
                failureAction,
                ex,
                reportException?.Invoke(ex));
            return false;
        }
    }

    public void HandlerMissing(
        ILogger logger,
        ZLinkDispatchErrorReporter dispatchErrors,
        LogLevel logLevel,
        ZLinkDispatchErrorAction action,
        Exception? exception = null)
    {
        ZLinkMessageFlowLogger.HandlerMissing(
            logger,
            logLevel,
            CreateEvent(ZLinkMessageFlowOutcome.Error, forLog: true),
            FormatAction(action),
            "no-handler",
            surfaceName,
            kindName,
            actorType,
            writeLog: false);
        if (action == ZLinkDispatchErrorAction.Drop)
            ReportDropped(
                dispatchErrors,
                ZLinkDispatchErrorReason.HandlerMissing,
                action,
                exception);
        else
            Report(
                dispatchErrors,
                ZLinkDispatchErrorReason.HandlerMissing,
                action,
                exception);
    }

    public void Dropped(
        ILogger logger,
        ZLinkDispatchErrorReporter dispatchErrors,
        LogLevel logLevel,
        ZLinkDispatchErrorReason reason = ZLinkDispatchErrorReason.HandlerMissing,
        string reasonName = "no-handler")
    {
        ZLinkMessageFlowLogger.Dropped(
            logger,
            logLevel,
            CreateEvent(ZLinkMessageFlowOutcome.Dropped, forLog: true),
            reasonName,
            surfaceName,
            kindName,
            actorType,
            writeLog: false);
        ReportDropped(
            dispatchErrors,
            reason,
            ZLinkDispatchErrorAction.Drop);
    }

    public void PayloadDecodeFailed(
        ILogger logger,
        ZLinkDispatchErrorReporter dispatchErrors,
        ZLinkDispatchErrorAction action,
        Exception exception,
        Exception? reportedException = null)
    {
        ZLinkMessageFlowLogger.PayloadDecodeFailed(
            logger,
            CreateEvent(ZLinkMessageFlowOutcome.Error, forLog: true),
            FormatAction(action),
            "payload-decode-failed",
            exception,
            surfaceName,
            kindName,
            actorType,
            writeLog: false);
        if (action == ZLinkDispatchErrorAction.Drop)
            ReportDropped(
                dispatchErrors,
                ZLinkDispatchErrorReason.PayloadDecodeFailed,
                action,
                reportedException ?? exception);
        else
            Report(
                dispatchErrors,
                ZLinkDispatchErrorReason.PayloadDecodeFailed,
                action,
                reportedException ?? exception);
    }

    public void HandlerException(
        ILogger logger,
        ZLinkDispatchErrorReporter dispatchErrors,
        LogLevel? logLevel,
        ZLinkDispatchErrorAction action,
        Exception exception)
    {
        if (logLevel is { } level)
            ZLinkMessageFlowLogger.Rejected(
                logger,
                level,
                CreateEvent(ZLinkMessageFlowOutcome.Error, forLog: true),
                "handler-exception",
                surfaceName,
                kindName,
                exception,
                actorType,
                writeLog: false);

        Report(
            dispatchErrors,
            ZLinkDispatchErrorReason.HandlerException,
            action,
            exception);
    }

    public void ReplyPathMissing(
        ILogger logger,
        ZLinkDispatchErrorReporter dispatchErrors,
        Exception exception)
    {
        ZLinkMessageFlowLogger.Rejected(
            logger,
            LogLevel.Error,
            CreateEvent(ZLinkMessageFlowOutcome.Error, forLog: true),
            "reply-path-missing",
            surfaceName,
            kindName,
            exception,
            actorType,
            writeLog: false);
        Report(
            dispatchErrors,
            ZLinkDispatchErrorReason.ReplyPathMissing,
            ZLinkDispatchErrorAction.FailCaller,
            exception);
    }

    public void Trace(
        ZLinkDispatchErrorReporter dispatchErrors,
        ZLinkMessageFlowOutcome outcome)
    {
        if (!dispatchErrors.Flow.Enabled(outcome)) return;

        dispatchErrors.Flow.Trace(CreateEvent(outcome, forLog: false));
    }

    private void Report(
        ZLinkDispatchErrorReporter dispatchErrors,
        ZLinkDispatchErrorReason reason,
        ZLinkDispatchErrorAction action,
        Exception? exception = null)
    {
        dispatchErrors.Report(new ZLinkDispatchFailure(
            surface,
            messageKind,
            reason,
            action,
            packetName,
            channelName,
            topic,
            SourceRid: sourceRid,
            SpotRid: spotRid,
            ActorId: actorId,
            CorrelationId: correlationId,
            Exception: exception));
    }

    private void ReportDropped(
        ZLinkDispatchErrorReporter dispatchErrors,
        ZLinkDispatchErrorReason reason,
        ZLinkDispatchErrorAction action,
        Exception? exception = null)
    {
        dispatchErrors.ReportDropped(new ZLinkDispatchFailure(
            surface,
            messageKind,
            reason,
            action,
            packetName,
            channelName,
            topic,
            SourceRid: sourceRid,
            SpotRid: spotRid,
            ActorId: actorId,
            CorrelationId: correlationId,
            Exception: exception));
    }

    private ZLinkMessageFlowEvent CreateEvent(
        ZLinkMessageFlowOutcome outcome,
        bool forLog)
    {
        var flow = ZLinkFlowContext.Current;
        return new ZLinkMessageFlowEvent(
            outcome,
            surface,
            messageKind,
            packetName,
            channelName,
            topic,
            correlationId,
            sourceRid,
            SpotRid: forLog ? logSpotRid ?? spotRid : spotRid,
            ActorId: actorId)
        {
            FlowId = flow?.FlowId ?? string.Empty,
            FlowOrigin = flow?.Origin
        };
    }

    private static string FormatAction(ZLinkDispatchErrorAction action)
    {
        return action switch
        {
            ZLinkDispatchErrorAction.Drop => "drop",
            ZLinkDispatchErrorAction.ReplyError => "reply-error",
            ZLinkDispatchErrorAction.FailCaller => "fail-caller",
            _ => action.ToString()
        };
    }
}
