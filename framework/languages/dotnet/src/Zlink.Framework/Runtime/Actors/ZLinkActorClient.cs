using Systems.Zlink.Stream.Connector.Runtime;
namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorClient(
    ZLinkFrameworkRuntime runtime) : IZLinkActorClient
{
    public IZLinkActorSendCall SendToActor<TMessage>(
        string meshName,
        ActorRef actor,
        TMessage message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        return new ZLinkActorSendCall<TMessage>(this, meshName, actor, message);
    }

    public IZLinkActorRequestCall RequestToActor<TRequest>(
        string meshName,
        ActorRef actor,
        TRequest request)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        return new ZLinkActorRequestCall<TRequest>(this, meshName, actor, request);
    }

    private async ValueTask<ZLinkOneWaySubmitResult> SubmitSendAsync<TMessage>(
        string meshName,
        ActorRef actor,
        string packetName,
        TMessage message,
        ZLinkCallMetadata metadata,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var authorityOwnerGeneration = await ResolveActorAuthorityAsync(
                meshName,
                actor,
                cancellationToken)
            .ConfigureAwait(false);
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        if (authorityOwnerGeneration != 0)
            nodeRuntime.ObserveActorAuthority(
                actor.ToBackend(),
                authorityOwnerGeneration);
        EnsureRouteAvailable(nodeRuntime, actor);
        var parts = CreatePacketParts(
            ZlinkStreamMessageKind.Send,
            null,
            packetName,
            message,
            metadata);
        try
        {
            TraceSent(actor, packetName, parts, ZLinkDispatchMessageKind.ActorSend);
            return await nodeRuntime.SendToActorAsync(actor.ToBackend(), parts, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException failure)
            when (ZLinkMeshCallSupport.TryMapSubmitFailure(failure, out var failed))
        {
            return failed;
        }
    }

    private async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        string meshName,
        ActorRef actor,
        string packetName,
        TRequest request,
        TimeSpan? timeout,
        ZLinkCallMetadata metadata,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation(countAsRequest: true);
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        var authorityOwnerGeneration = await ResolveActorAuthorityAsync(
                meshName,
                actor,
                cancellationToken)
            .ConfigureAwait(false);
        var nodeRuntime = await GetActorSpotNodeAsync(meshName, cancellationToken).ConfigureAwait(false);
        if (authorityOwnerGeneration != 0)
            nodeRuntime.ObserveActorAuthority(
                actor.ToBackend(),
                authorityOwnerGeneration);
        EnsureRouteAvailable(nodeRuntime, actor);
        var node = nodeRuntime.Node;
        var parts = CreatePacketParts(
            ZlinkStreamMessageKind.Request,
            new ZlinkStreamRequestSeq(1),
            packetName,
            request,
            metadata);
        TraceSent(actor, packetName, parts, ZLinkDispatchMessageKind.ActorRequest);
        return await SubmitActorRequestAsync<TReply>(
                node,
                actor.ToBackend(),
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ulong> ResolveActorAuthorityAsync(
        string meshName,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        var store = runtime.Registration.Locations.ResolveStore();
        if (store is null) return 0;
        var read = await store.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(actor.ActorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor route '{actor.ActorId}' was not found.");
        var snapshot = found.Snapshot;
        if (snapshot.Allocation.State != ZLinkPlacementAllocationState.Active
            || snapshot.Allocation.ObjectKind != ZLinkPlacementObjectKind.Actor
            || !ZLinkActorAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span, out var authority)
            || authority.State != ZLinkActorAuthorityState.Ready)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor route '{actor.ActorId}' is not ready.",
                true);
        if (!string.Equals(authority.MeshName, meshName, StringComparison.Ordinal)
            || authority.NodeRid != actor.NodeRid
            || snapshot.ObjectGeneration != actor.Generation)
        {
            runtime.LogActorHandoff(
                $"stale_fail_fast actor={actor.ActorId} generation={actor.Generation}");
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref for '{actor.ActorId}' does not match the current location generation.",
                true);
        }
        if (snapshot.AuthorityOwnerGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor route '{actor.ActorId}' does not carry an authority owner generation.",
                true);
        return snapshot.AuthorityOwnerGeneration;
    }

    private void TraceSent(
        ActorRef actor,
        string packetName,
        IReadOnlyList<Message> parts,
        ZLinkDispatchMessageKind messageKind)
    {
        if (!runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent)) return;

        var header = ZLinkStreamProtocolDefaults.DecodeHeader(parts[0].AsReadOnlyMemory());
        runtime.Flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchErrorSurface.SpotActor,
            messageKind,
            packetName,
            CorrelationId: header.CorrelationId,
            ActorId: actor.ActorId));
    }

    private async ValueTask<TReply> SubmitActorRequestAsync<TReply>(
        IZLinkBackendSpotNode node,
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        IReadOnlyList<Message> reply;
        try
        {
            reply = await node.RequestToActorAsync(
                    actor,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZlinkSubmitException error)
        {
            throw MapSubmitException(error, "Actor request");
        }
        catch (ZlinkRequestException error)
        {
            throw MapRequestException(error, "Actor request");
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        try
        {
            return ZLinkActorReplyDecoder.Decode<TReply>(reply);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    private async ValueTask<ZLinkSpotNodeRuntime> GetActorSpotNodeAsync(
        string meshName,
        CancellationToken cancellationToken)
    {
        await runtime.EnsureStartedStateAsync(cancellationToken).ConfigureAwait(false);
        return runtime.GetMeshNodeRuntime(meshName);
    }

    private static void EnsureRouteAvailable(
        ZLinkSpotNodeRuntime node,
        ActorRef actor)
    {
        if (actor.NodeRid == node.Node.RoutingId
            || !node.IsExplicitManualRouterRouteDisconnected(actor.NodeRid))
            return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RouteNotConnected,
            $"Actor route to node '{actor.NodeRid}' is not connected.",
            true);
    }

    private static IReadOnlyList<Message> CreatePacketParts<TMessage>(
        ZlinkStreamMessageKind kind,
        ZlinkStreamRequestSeq? requestSeq,
        string packetName,
        TMessage message,
        ZLinkCallMetadata? callMetadata = null)
    {
        var metadata = callMetadata?.ToStreamMetadata() ?? ZlinkStreamMetadata.Empty;
        var flags = requestSeq is null
            ? ZlinkStreamHeaderFlags.None
            : ZlinkStreamHeaderFlags.HasRequestSeq;
        if (metadata.Count != 0) flags |= ZlinkStreamHeaderFlags.HasMetadata;
        var header = new ZlinkStreamHeader(
            kind,
            ZlinkStreamCodec.Json,
            flags,
            requestSeq,
            packetName,
            metadata,
            ZlinkStreamCorrelation.Next());
        var payload = ZLinkEnvelopeCodec.EncodeJsonBytes(message, message?.GetType() ?? typeof(TMessage));
        return
        [
            Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span),
            Message.From(payload)
        ];
    }

    private static Exception MapSubmitException(
        ZlinkSubmitException error,
        string operationName)
    {
        return error.Result switch
        {
            ZlinkSubmitException.ErrorCode.NotConnected => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"{operationName} failed because the target route is not connected.",
                true,
                error),
            ZlinkSubmitException.ErrorCode.NotFound => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"{operationName} failed because the actor route was not found.",
                innerException: error),
            _ => ZLinkRequestFailureMapper.CreateSubmitException(error, operationName)
        };
    }

    private static Exception MapRequestException(
        ZlinkRequestException error,
        string operationName)
    {
        return error.Result switch
        {
            ZlinkRequestException.ErrorCode.NotConnected => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"{operationName} failed because the target route is not connected.",
                true,
                error),
            ZlinkRequestException.ErrorCode.NotFound => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"{operationName} failed because the actor route was not found.",
                innerException: error),
            ZlinkRequestException.ErrorCode.Conflict => new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"{operationName} failed because the actor location is stale.",
                true,
                error),
            _ => ZLinkRequestFailureMapper.CreateCompletionException(
                (RequestResult)(int)error.Result,
                operationName)
        };
    }

    private sealed class ZLinkActorSendCall<TMessage>(
        ZLinkActorClient client,
        string meshName,
        ActorRef actor,
        TMessage message) : IZLinkActorSendCall
    {
        private readonly ZLinkCallMetadata _metadata = new();
        private readonly ZLinkOneWayCallGate _submission = new("Actor send");

        public IZLinkActorSendCall Metadata(string key, string value)
        {
            _metadata.Set(key, value);
            return this;
        }

        public IZLinkActorSendCall Metadata(ZLinkMessageMetadata metadata)
        {
            _metadata.Merge(metadata);
            return this;
        }

        public ValueTask Async(
            CancellationToken cancellationToken = default)
        {
            _submission.Claim();
            return client.SubmitSendAsync(
                meshName,
                actor,
                ZLinkMessageNameResolver.ResolveFromMessage(message),
                message,
                _metadata,
                cancellationToken).EnsureAcceptedAsync(
                    "Actor send",
                    ZLinkFrameworkErrorKind.ActorRouteNotFound);
        }
    }

    private sealed class ZLinkActorRequestCall<TRequest>(
        ZLinkActorClient client,
        string meshName,
        ActorRef actor,
        TRequest request) : IZLinkActorRequestCall
    {
        private readonly ZLinkCallMetadata _metadata = new();
        private readonly ZLinkApplicationExecutionScope? _executionScope =
            ZLinkApplicationExecutionContext.Current;
        private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
        private TimeSpan? _timeout;

        public IZLinkActorRequestCall Timeout(TimeSpan timeout)
        {
            ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
            _timeout = timeout;
            return this;
        }

        public IZLinkActorRequestCall Metadata(string key, string value)
        {
            _metadata.Set(key, value);
            return this;
        }

        public IZLinkActorRequestCall Metadata(ZLinkMessageMetadata metadata)
        {
            _metadata.Merge(metadata);
            return this;
        }

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        {
            ZLinkApplicationExecutionContext.RejectActorRequestWhenSameClaim(
                actor.ActorId,
                _executionScope);
            return ExecuteAsync<TReply>(cancellationToken);
        }

        public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
        {
            ZLinkApplicationExecutionContext.RejectActorRequestWhenSameClaim(
                actor.ActorId,
                _executionScope);
            return ZLinkApplicationExecutionContext
                .RequireYieldTurn(_turn, "Actor request")
                .YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
        }

        private ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
        {
            return client.RequestAsync<TRequest, TReply>(
                meshName,
                actor,
                ZLinkMessageNameResolver.ResolveFromMessage(request),
                request,
                _timeout,
                _metadata,
                cancellationToken);
        }
    }
}
