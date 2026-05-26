using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivationDispatcher(
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendSpot nativeSpot,
    string channelName,
    ZLinkSpotPacketRegistry packets,
    ZLinkSpotActorJoinRegistry actorJoins,
    ZLinkSpotActorMembership actors,
    ZLinkSpotSubscriptionRegistry subscriptions,
    Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker)
{
    private readonly ZLinkSpotActorPacketDispatcher _actorPacketDispatcher =
        new(runtime, actorHandlers, handlerInvoker);
    private readonly ZLinkSpotActorJoinDispatcher _actorJoinDispatcher =
        new(
            runtime,
            nativeSpot,
            channelName,
            actorJoins,
            actors,
            handlerInvoker,
            runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkSpotActorJoinDispatcher>());
    private readonly ZLinkSpotRouteDispatcher _routeDispatcher =
        new(
            channelName,
            packets,
            handlerInvoker,
            runtime.Services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkSpotRouteDispatcher>());

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

            if (request is null)
            {
                return;
            }

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
        int i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            if (!actors.TryGetActor(headerPart.Actor.ActorId, out var actor) || actor is null)
            {
                actor = runtime.GetOrCreateActorState(headerPart.Actor.ActorId).Actor;
            }

            if (actor is null)
            {
                ZLinkSpotActorFrameReader.DisposeFrame(parts, ref i, headerPart);
                continue;
            }

            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref i, headerPart, out var frame))
            {
                continue;
            }

            using (frame.Body)
            {
                await DispatchActorStreamPartAsync(
                        actor,
                        frame.Actor.ActorId,
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
            var received = new Received();
            if (!nativeSpot.RecvRoute(received, RecvFlags.DontWait))
            {
                received.Dispose();
                return;
            }

            await _routeDispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
        }
    }

    public async ValueTask DispatchRouteAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        await _routeDispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await subscriptions
            .DrainAsync(nativeSpot, InvokeSubscriptionAsync, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchActorStreamPartAsync(
        IZLinkActor actor,
        string actorId,
        RoutingId sourceSessionRid,
        ZlinkStreamHeader streamHeader,
        Message body,
        CancellationToken cancellationToken)
    {
        var runtimeState = runtime.GetOrCreateActorState(actorId);
        await using var boundSessionScope = ZLinkBoundSessionDispatchScope.Enter(actorId);
        runtime.BindActorSession(
            actorId,
            sourceSessionRid,
            BuildNativeBoundSessionToken(sourceSessionRid));
        if (streamHeader.RequestSeq is { })
        {
            var reply = await _actorPacketDispatcher.DispatchForReplyAsync(
                    actor,
                    runtimeState,
                    streamHeader,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
            if (reply is null)
            {
                return;
            }

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
        var timeout = runtime.Registration.DefaultTimeout;
        var retryDelay = TimeSpan.FromMilliseconds(25);
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
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
                {
                    return;
                }
            }
            catch (ZlinkSubmitException error) when (error.Result == ZlinkSubmitException.ErrorCode.NotConnected)
            {
                lastError = error;
            }

            if (elapsed.Elapsed >= timeout)
            {
                throw new InvalidOperationException("Actor request reply relay failed.", lastError);
            }

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
        await handlerInvoker().InvokeSubscriptionAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

}
