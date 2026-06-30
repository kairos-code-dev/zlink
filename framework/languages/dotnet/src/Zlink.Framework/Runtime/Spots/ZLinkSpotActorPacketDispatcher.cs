using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorPacketDispatcher(
    Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker,
    ZLinkDispatchErrorReporter dispatchErrors,
    ILogger logger)
{
    public async ValueTask DispatchAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        using var dispatch = runtimeState.EnterDispatch(header);

        if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Received))
            dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Received,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.ActorSend,
                header.Name,
                ActorId: actor.ActorId,
                CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString()));

        if (TryResolveActorPacketDescriptor(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
        {
            try
            {
                await handlerInvoker()
                    .InvokeActorPacketAsync(descriptor, actor, header, body, cancellationToken)
                    .ConfigureAwait(false);

                if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Dispatched))
                    dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.Dispatched,
                        ZLinkDispatchErrorSurface.SpotActor,
                        ZLinkDispatchMessageKind.ActorSend,
                        header.Name,
                        ActorId: actor.ActorId,
                        CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString()));
            }
            catch (Exception ex)
            {
                ZLinkMessageFlowLogger.Rejected(
                    logger,
                    LogLevel.Error,
                    "SpotActor",
                    "ActorSend",
                    header.Name,
                    "handler-exception",
                    ex,
                    actorId: actor.ActorId,
                    actorType: actor.GetType().FullName);
                dispatchErrors.Report(new ZLinkDispatchFailure(
                    ZLinkDispatchErrorSurface.SpotActor,
                    ZLinkDispatchMessageKind.ActorSend,
                    ZLinkDispatchErrorReason.HandlerException,
                    ZLinkDispatchErrorAction.Drop,
                    header.Name,
                    ActorId: actor.ActorId,
                    CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString(),
                    Exception: ex));
            }

            return;
        }

        ZLinkMessageFlowLogger.Dropped(
            logger,
            LogLevel.Warning,
            "SpotActor",
            "ActorSend",
            header.Name,
            "no-handler",
            actorId: actor.ActorId,
            actorType: actor.GetType().FullName);
        dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.SpotActor,
            ZLinkDispatchMessageKind.ActorSend,
            ZLinkDispatchErrorReason.HandlerMissing,
            ZLinkDispatchErrorAction.Drop,
            header.Name,
            ActorId: actor.ActorId,
            CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString()));
    }

    public async ValueTask<ZLinkActorReply?> DispatchForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        using var dispatch = runtimeState.EnterDispatch(header);

        if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Received))
            dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Received,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.ActorRequest,
                header.Name,
                ActorId: actor.ActorId,
                CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString()));

        if (TryResolveActorPacketDescriptor(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
            try
            {
                var reply = await handlerInvoker()
                    .InvokeActorPacketForReplyAsync(descriptor, actor, header, body, cancellationToken)
                    .ConfigureAwait(false);

                if (dispatchErrors.Flow.Enabled(ZLinkMessageFlowOutcome.Replied))
                    dispatchErrors.Flow.Trace(new ZLinkMessageFlowEvent(
                        ZLinkMessageFlowOutcome.Replied,
                        ZLinkDispatchErrorSurface.SpotActor,
                        ZLinkDispatchMessageKind.ActorRequest,
                        header.Name,
                        ActorId: actor.ActorId,
                        CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString()));

                return reply;
            }
            catch (Exception ex)
            {
                ZLinkMessageFlowLogger.Rejected(
                    logger,
                    LogLevel.Error,
                    "SpotActor",
                    "ActorRequest",
                    header.Name,
                    "handler-exception",
                    ex,
                    actorId: actor.ActorId,
                    actorType: actor.GetType().FullName);
                dispatchErrors.Report(new ZLinkDispatchFailure(
                    ZLinkDispatchErrorSurface.SpotActor,
                    ZLinkDispatchMessageKind.ActorRequest,
                    ZLinkDispatchErrorReason.HandlerException,
                    ZLinkDispatchErrorAction.ReplyError,
                    header.Name,
                    ActorId: actor.ActorId,
                    CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString(),
                    Exception: ex));
                throw;
            }

        ZLinkMessageFlowLogger.HandlerMissing(
            logger,
            LogLevel.Error,
            "SpotActor",
            "ActorRequest",
            header.Name,
            "reply-error",
            "no-handler",
            actorId: actor.ActorId,
            actorType: actor.GetType().FullName);
        dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.SpotActor,
            ZLinkDispatchMessageKind.ActorRequest,
            ZLinkDispatchErrorReason.HandlerMissing,
            ZLinkDispatchErrorAction.ReplyError,
            header.Name,
            ActorId: actor.ActorId,
            CorrelationId: header.CorrelationId ?? header.RequestSeq?.ToString(),
            Exception: new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
                $"No Spot actor request handler is registered for '{header.Name}'.")));
        return null;
    }

    private bool TryResolveActorPacketDescriptor(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        descriptor = null;
        return actorHandlers() is { } handlers
               && handlers.TryResolve(actorType, header, out descriptor);
    }
}