using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivationDispatcher
{
    private readonly ZLinkSpotActorJoinDispatcher _actorJoinDispatcher;
    private readonly ZLinkSpotActorPacketDispatcher _actorPacketDispatcher;
    private readonly ZLinkDispatchErrorReporter _dispatchErrors;
    private readonly ILogger<ZLinkSpotActivationDispatcher> _logger;
    private readonly ZLinkSpotRouteDispatcher _routeDispatcher;
    private readonly ZLinkSpotActorMembership actors;
    private readonly string channelName;
    private readonly Func<ZLinkSpotHandlerInvoker> handlerInvoker;
    private readonly IZLinkBackendSpot nativeSpot;
    private readonly ZLinkFrameworkRuntime runtime;
    private readonly ZLinkSpotSubscriptionRegistry subscriptions;

    public ZLinkSpotActivationDispatcher(
        ZLinkFrameworkRuntime runtime,
        IZLinkBackendSpot nativeSpot,
        string channelName,
        ZLinkSpotPacketRegistry packets,
        ZLinkSpotActorJoinRegistry actorJoins,
        ZLinkSpotActorMembership actors,
        ZLinkSpotSubscriptionRegistry subscriptions,
        Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
        Func<ZLinkSpotHandlerInvoker> handlerInvoker,
        Func<IZLinkActor, CancellationToken, ValueTask>? commitAcceptedActorJoin = null)
    {
        this.runtime = runtime;
        this.nativeSpot = nativeSpot;
        this.channelName = channelName;
        this.actors = actors;
        this.subscriptions = subscriptions;
        this.handlerInvoker = handlerInvoker;
        _logger = runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkSpotActivationDispatcher>()
                  ?? NullLogger<ZLinkSpotActivationDispatcher>.Instance;
        _dispatchErrors = new ZLinkDispatchErrorReporter(
            runtime.Registration.DispatchOptions,
            runtime.Services,
            _logger);
        _actorPacketDispatcher = new ZLinkSpotActorPacketDispatcher(
            actorHandlers,
            handlerInvoker,
            _dispatchErrors,
            runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkSpotActorPacketDispatcher>()
            ?? NullLogger<ZLinkSpotActorPacketDispatcher>.Instance);
        _actorJoinDispatcher = new ZLinkSpotActorJoinDispatcher(
            runtime,
            nativeSpot,
            channelName,
            actorJoins,
            actors,
            handlerInvoker,
            runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkSpotActorJoinDispatcher>(),
            commitAcceptedActorJoin);
        _routeDispatcher = new ZLinkSpotRouteDispatcher(
            channelName,
            nativeSpot.RoutingId.ToString(),
            packets,
            handlerInvoker,
            runtime.Registration.Codecs,
            _dispatchErrors,
            DispatchInternalRoutePacketAsync,
            runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkSpotRouteDispatcher>());
    }

    public ZLinkSpotActorPacketDispatcher ActorPackets => _actorPacketDispatcher;

    public async ValueTask DispatchActorJoinDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            ZLinkBackendActorJoinRequest? request;
            try
            {
                request = nativeSpot.RecvActorJoin(RecvFlags.DontWait);
            }
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                return;
            }

            if (request is null) return;

            try
            {
                await _actorJoinDispatcher.DispatchAsync(request, cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(request.Parts);
            }
        }
    }

    public async ValueTask DispatchActorPartsAsync(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        var i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            var runtimeState = runtime.GetOrCreateActorState(headerPart.Actor.ActorId);
            if (!actors.TryGetActor(headerPart.Actor.ActorId, out var actor) || actor is null)
                actor = runtimeState.Actor;

            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref i, headerPart, out var frame)) continue;

            if (actor is null)
            {
                using (frame.Body)
                {
                    ZLinkActorBoundSessionRelay.TryReplyMissingNoBindActor(
                        runtime,
                        frame.Actor,
                        frame.SourceNodeRid,
                        frame.SourceSessionRid,
                        frame.RequestId,
                        frame.Flags,
                        frame.Header);
                }

                continue;
            }

            if (ZLinkActorSessionForwarder.ShouldForward(
                    runtimeState,
                    frame.Actor,
                    out var targetActor))
            {
                using (frame.Body)
                {
                    ZLinkActorSessionForwarder.Forward(
                        runtime,
                        targetActor,
                        frame.SourceNodeRid,
                        frame.SourceSessionRid,
                        frame.Header,
                        frame.Body);
                }

                continue;
            }

            using (frame.Body)
            {
                await DispatchActorStreamPartAsync(
                        actor,
                        frame.Actor,
                        frame.Actor.ActorId,
                        frame.SourceNodeRid,
                        frame.SourceSessionRid,
                        frame.RequestId,
                        frame.Flags,
                        frame.Header,
                        frame.Body,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
        }
    }

    public async ValueTask DispatchRouteDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var received = Received.Create();
            if (!nativeSpot.RecvRoute(received, RecvFlags.DontWait))
            {
                received.Dispose();
                return;
            }

            await _routeDispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
            runtime.DrainSpotRouteBridges();
        }
    }

    public async ValueTask DispatchRouteAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        await _routeDispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
        runtime.DrainSpotRouteBridges();
    }

    private async ValueTask<bool> DispatchInternalRoutePacketAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(
                header.MessageName,
                ZLinkRemoteActorJoinPackets.RequestPacketName,
                StringComparison.Ordinal))
            return false;

        if (received.Parts.Count < 2)
        {
            ReplyInternalRouteError(
                received,
                header,
                new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                    "Remote actor join request body part is missing."));
            return true;
        }

        ZLinkRemoteActorJoinReply reply;
        try
        {
            var joinRequest = ZLinkRemoteActorJoinPackets.DecodeJoinRequest(received.Parts);
            reply = await runtime.JoinRoutedActorAsync(
                    nativeSpot.RoutingId,
                    joinRequest,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            ReplyInternalRouteError(received, header, ex);
            return true;
        }

        var replyParts = ZLinkRemoteActorJoinPackets.EncodeJoinReplyEnvelope(
            channelName,
            header.MessageName,
            header.CorrelationId,
            reply);
        ZLinkSpotReplySubmitter.SubmitAndDispose(received, replyParts);

        return true;
    }

    private void ReplyInternalRouteError(
        Received received,
        ZLinkEnvelopeHeader header,
        Exception exception)
    {
        var replyParts = ZLinkSpotReplyEnvelope.EncodeErrorParts(
            channelName,
            header.MessageName,
            header.CorrelationId,
            exception);
        ZLinkSpotReplySubmitter.SubmitAndDispose(received, replyParts);
    }

    public async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await subscriptions
            .DrainAsync(nativeSpot, runtime.Registration.Codecs, _dispatchErrors, _logger, InvokeSubscriptionAsync,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchActorStreamPartAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        string actorId,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader streamHeader,
        Message body,
        CancellationToken cancellationToken)
    {
        if (ZLinkActorBoundSessionRelay.IsSessionDisconnectedPacket(streamHeader))
        {
            ZLinkActorBoundSessionRelay.RemoveNativeBinding(runtime, actorId, sourceSessionRid);
            await runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var runtimeState = runtime.GetOrCreateActorState(actorId);
        var boundSession = ZLinkActorBoundSessionRelay.EnterDispatch(
            runtime,
            actorId,
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags);

        try
        {
            if (streamHeader.RequestSeq is not null)
            {
                var reply = await _actorPacketDispatcher.DispatchForReplyAsync(
                        actor,
                        runtimeState,
                        streamHeader,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (reply is null) return;

                await ZLinkActorBoundSessionRelay.SendReplyAsync(
                        runtime,
                        actorId,
                        actorRef,
                        sourceNodeRid,
                        sourceSessionRid,
                        requestId,
                        flags,
                        boundSession.IsNoBind,
                        streamHeader,
                        reply,
                        cancellationToken)
                    .ConfigureAwait(false);

                await boundSession.DrainAsync(cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await _actorPacketDispatcher.DispatchAsync(
                    actor,
                    runtimeState,
                    streamHeader,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            await boundSession.DisposeAsync().ConfigureAwait(false);
        }
    }

    private async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        try
        {
            await handlerInvoker().InvokeSubscriptionAsync(descriptor, message, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            var scope = new ZLinkDispatchFlowScope(
                ZLinkDispatchErrorSurface.SpotSubscription,
                "SpotSubscription",
                ZLinkDispatchMessageKind.Publish,
                "Publish",
                descriptor.MessageName,
                topic: descriptor.Topic);
            scope.HandlerException(
                _logger,
                _dispatchErrors,
                LogLevel.Error,
                ZLinkDispatchErrorAction.Drop,
                ex);
        }
    }
}
