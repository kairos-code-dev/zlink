using Systems.Zlink.Stream.Connector.Protocol;
using Zlink.Framework.Backend.Contracts;

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
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

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

            await DispatchActorJoinAsync(request, cancellationToken).ConfigureAwait(false);
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
                headerPart.Message.Dispose();
                while (i < parts.Count && parts[i - 1].More)
                {
                    parts[i++].Message.Dispose();
                }
                continue;
            }

            if (!headerPart.More)
            {
                await DispatchActorStreamPartAsync(
                    actor,
                    headerPart.Actor.ActorId,
                    HeaderCodec.Decode(headerPart.Message.AsReadOnlyMemory()),
                    Message.FromBytes(ReadOnlySpan<byte>.Empty),
                    cancellationToken).ConfigureAwait(false);
                headerPart.Message.Dispose();
                continue;
            }

            if (i >= parts.Count)
            {
                headerPart.Message.Dispose();
                continue;
            }

            var bodyPart = parts[i++];
            var streamHeader = HeaderCodec.Decode(headerPart.Message.AsReadOnlyMemory());
            headerPart.Message.Dispose();
            using var body = bodyPart.Message;
            await DispatchActorStreamPartAsync(actor, headerPart.Actor.ActorId, streamHeader, body, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask DispatchActorPacketAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previousDispatch = runtimeState.CurrentDispatch;
        runtimeState.CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            if (TryResolveActorPacketDescriptor(actor.GetType(), header, out var descriptor)
                && descriptor is not null)
            {
                await handlerInvoker()
                    .InvokeActorPacketAsync(descriptor, actor, header, body, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await runtimeState.DispatchAsync(
                    runtime.Services,
                    actor,
                    header,
                    body.Move(),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            runtimeState.CurrentDispatch = previousDispatch;
        }
    }

    public async ValueTask<byte[]?> DispatchActorPacketForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previousDispatch = runtimeState.CurrentDispatch;
        runtimeState.CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            if (TryResolveActorPacketDescriptor(actor.GetType(), header, out var descriptor)
                && descriptor is not null)
            {
                return await handlerInvoker()
                    .InvokeActorPacketForReplyAsync(descriptor, actor, header, body, cancellationToken)
                    .ConfigureAwait(false);
            }

            return await runtimeState.DispatchForReplyAsync(
                    runtime.Services,
                    actor,
                    header,
                    body.Move(),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            runtimeState.CurrentDispatch = previousDispatch;
        }
    }

    public async ValueTask DispatchRouteDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            Received received;
            try
            {
                received = nativeSpot.RecvRoute(RecvFlags.DontWait);
            }
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                return;
            }

            await DispatchRouteAsync(received, cancellationToken);
        }
    }

    public async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await subscriptions
            .DrainAsync(nativeSpot, InvokeSubscriptionAsync, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchActorJoinAsync(
        ZLinkBackendActorJoinRequest joinRequest,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(joinRequest.Message);
        if (!actorJoins.TryResolveByName(header.MessageName, out var descriptor) || descriptor is null)
        {
            ReplyActorJoinRejected(joinRequest);
            return;
        }

        if (!actors.TryGetActor(joinRequest.TargetActor.ActorId, out var actor) || actor is null)
        {
            actor = runtime.GetOrCreateActorState(joinRequest.TargetActor.ActorId).Actor;
        }

        if (actor is null)
        {
            ReplyActorJoinRejected(joinRequest);
            return;
        }

        var requestObj = ZLinkEnvelopeCodec.DecodeBody(joinRequest.Message, descriptor.RequestType)!;
        var replyObj = await InvokeActorJoinAsync(descriptor, actor, requestObj, cancellationToken)
            .ConfigureAwait(false);

        var replyEnvelopeHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Response,
            channelName,
            descriptor.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null, null, null, null, null);
        using var replyMessage = ZLinkEnvelopeCodec.Encode(replyEnvelopeHeader, replyObj, descriptor.ReplyType);
        nativeSpot.ReplyActorJoin(joinRequest, accepted: true, replyMessage);
    }

    private async ValueTask DispatchActorStreamPartAsync(
        IZLinkActor actor,
        string actorId,
        ZlinkStreamHeader streamHeader,
        Message body,
        CancellationToken cancellationToken)
    {
        var runtimeState = runtime.GetOrCreateActorState(actorId);
        var previousDispatch = runtimeState.CurrentDispatch;
        runtimeState.CurrentDispatch = new ZLinkActorDispatchState(streamHeader);
        try
        {
            if (TryResolveActorPacketDescriptor(actor.GetType(), streamHeader, out var descriptor)
                && descriptor is not null)
            {
                await handlerInvoker()
                    .InvokeActorPacketAsync(descriptor, actor, streamHeader, body, cancellationToken)
                    .ConfigureAwait(false);
            }
            else
            {
                await runtimeState.DispatchAsync(runtime.Services, actor, streamHeader, body, cancellationToken)
                    .ConfigureAwait(false);
            }
        }
        finally
        {
            runtimeState.CurrentDispatch = previousDispatch;
        }
    }

    private async ValueTask DispatchRouteAsync(Received received, CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Parts.Count == 0)
            {
                return;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts[0]);
            if (!packets.TryResolve(header, out var descriptor) || descriptor is null)
            {
                return;
            }

            var message = ZLinkEnvelopeCodec.DecodeBody(received.Parts[0], descriptor.MessageType);
            if (descriptor.IsRequest)
            {
                var reply = await InvokeRequestAsync(descriptor, message, cancellationToken);
                var replyHeader = new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Response,
                    channelName,
                    descriptor.MessageName,
                    ZLinkEnvelopeCodec.DefaultContentType,
                    header.CorrelationId,
                    null,
                    null,
                    null,
                    null);
                using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, descriptor.ReplyType);
                received.Reply()
                    .Message(replyMessage)
                    .Submit();
                return;
            }

            await InvokePacketAsync(descriptor, message, cancellationToken);
        }
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

    private void ReplyActorJoinRejected(ZLinkBackendActorJoinRequest joinRequest)
    {
        using var emptyReply = Message.FromBytes(ReadOnlySpan<byte>.Empty);
        nativeSpot.ReplyActorJoin(joinRequest, accepted: false, emptyReply);
    }

    private async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await handlerInvoker().InvokePacketAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await handlerInvoker().InvokeRequestAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await handlerInvoker().InvokeSubscriptionAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        return await handlerInvoker().InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
    }
}
