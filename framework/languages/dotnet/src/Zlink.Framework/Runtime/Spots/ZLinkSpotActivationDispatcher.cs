using System.Diagnostics;
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

            if (actor is null)
            {
                ZLinkSpotActorFrameReader.DisposeFrame(parts, ref i, headerPart);
                continue;
            }

            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref i, headerPart, out var frame)) continue;

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
                        frame.Actor.ActorId,
                        frame.SourceNodeRid,
                        frame.SourceSessionRid,
                        frame.Header,
                        frame.Body,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
        }
    }

    public async ValueTask DispatchActorPacketAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await _actorPacketDispatcher.DispatchAsync(
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorReply?> DispatchActorPacketForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return await _actorPacketDispatcher.DispatchForReplyAsync(
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
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
            var joinRequest = ZLinkEnvelopeCodec.DecodePart<ZLinkRemoteActorJoinRequest>(received.Parts[1]);
            reply = await runtime.JoinRoutedActorAsync(
                    joinRequest.ActorId,
                    joinRequest.ActorType,
                    nativeSpot.RoutingId,
                    ToRoutingId(joinRequest.BoundSessionNodeRid),
                    ToRoutingId(joinRequest.BoundSessionRid),
                    joinRequest.RequestContentType,
                    joinRequest.Request,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            ReplyInternalRouteError(received, header, ex);
            return true;
        }

        var replyParts = ZLinkSpotReplyEnvelope.EncodeResponseParts(
            channelName,
            header.MessageName,
            header.CorrelationId,
            reply,
            typeof(ZLinkRemoteActorJoinReply));
        try
        {
            received.Reply()
                .Message(replyParts[0])
                .Message(replyParts[1])
                .Submit();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }

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
        try
        {
            received.Reply()
                .Message(replyParts[0])
                .Message(replyParts[1])
                .Submit();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }

    private static RoutingId? ToRoutingId(byte[]? bytes)
    {
        return bytes is { Length: > 0 } ? RoutingId.From(bytes) : null;
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
        string actorId,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ZlinkStreamHeader streamHeader,
        Message body,
        CancellationToken cancellationToken)
    {
        if (string.Equals(
                streamHeader.Name,
                ZLinkRemoteActorJoinPackets.SessionDisconnectedPacketName,
                StringComparison.Ordinal))
        {
            runtime.RemoveActorSessionBinding(actorId, BuildNativeBoundSessionToken(sourceSessionRid));
            await runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var runtimeState = runtime.GetOrCreateActorState(actorId);
        await using var boundSessionScope = ZLinkBoundSessionDispatchScope.Enter(actorId);
        runtime.BindActorSession(
            actorId,
            sourceNodeRid,
            sourceSessionRid,
            BuildNativeBoundSessionToken(sourceSessionRid));
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

            var frame = reply.ToFrame(streamHeader);
            await SendFrameWithRetryAsync(runtime, actorId, frame, cancellationToken)
                .ConfigureAwait(false);

            await boundSessionScope.DrainAsync(cancellationToken)
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

    private static async ValueTask SendFrameWithRetryAsync(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var timeout = runtime.Registration.DefaultRequestTimeout;
        var retryDelay = TimeSpan.FromMilliseconds(25);
        var elapsed = Stopwatch.StartNew();
        Exception? lastError = null;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            using var frameMessage = Message.From(frame);
            try
            {
                if (runtime.SendActorBoundSession(
                        actorId,
                        new[] { frameMessage },
                        SendFlags.None))
                    return;
            }
            catch (ZlinkSubmitException error) when (error.Result == ZlinkSubmitException.ErrorCode.NotConnected)
            {
                lastError = error;
            }

            if (elapsed.Elapsed >= timeout)
                throw new InvalidOperationException("Actor request reply relay failed.", lastError);

            var remaining = timeout - elapsed.Elapsed;
            await Task.Delay(remaining < retryDelay ? remaining : retryDelay, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static string BuildNativeBoundSessionToken(RoutingId sourceSessionRid)
    {
        return $"native:{sourceSessionRid.ToHex()}";
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
            ZLinkMessageFlowLogger.Rejected(
                _logger,
                LogLevel.Error,
                "SpotSubscription",
                "Publish",
                descriptor.MessageName,
                "handler-exception",
                ex,
                descriptor.Topic);
            _dispatchErrors.Report(new ZLinkDispatchFailure(
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                ZLinkDispatchErrorReason.HandlerException,
                ZLinkDispatchErrorAction.Drop,
                descriptor.MessageName,
                Topic: descriptor.Topic,
                Exception: ex));
        }
    }
}
