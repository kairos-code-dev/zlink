using System.Text.Json;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorClient(
    ZLinkFrameworkRuntime runtime) : IZLinkActorClient
{
    public IZLinkActorSendCall SendToActor<TMessage>(
        ActorRef actor,
        TMessage message)
    {
        return new ZLinkActorSendCall<TMessage>(this, actor, message);
    }

    public IZLinkActorRequestCall RequestToActor<TRequest>(
        ActorRef actor,
        TRequest request)
    {
        return new ZLinkActorRequestCall<TRequest>(this, actor, request);
    }

    private async ValueTask SendAsync<TMessage>(
        ActorRef actor,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        var parts = CreatePacketParts(
            ZlinkStreamMessageKind.Send,
            null,
            packetName,
            message);
        await SubmitActorSendAsync(actor.ToBackend(), parts, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        ActorRef actor,
        string packetName,
        TRequest request,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        var parts = CreatePacketParts(
            ZlinkStreamMessageKind.Request,
            new ZlinkStreamRequestSeq(1),
            packetName,
            request);
        return await SubmitActorRequestAsync<TReply>(
                actor.ToBackend(),
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask SubmitActorSendAsync(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var node = await GetActorSpotNodeAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!node.SendToActor(actor, parts, SendFlags.None))
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    "Actor send failed because the target route is not connected.",
                    true);
            }
        }
        catch (ZlinkSubmitException error)
        {
            throw MapSubmitException(error, "Actor send");
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    private async ValueTask<TReply> SubmitActorRequestAsync<TReply>(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var node = await GetActorSpotNodeAsync(cancellationToken).ConfigureAwait(false);
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
            return DecodeReply<TReply>(reply);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    private async ValueTask<IZLinkBackendSpotNode> GetActorSpotNodeAsync(
        CancellationToken cancellationToken)
    {
        var state = await runtime.EnsureStartedStateAsync(cancellationToken).ConfigureAwait(false);
        lock (state.SyncRoot)
        {
            return state.SpotNodes.Values.FirstOrDefault()?.Node
                   ?? throw new ZLinkConfigurationException(
                       "Actor client requires a configured SPOT node.");
        }
    }

    private static IReadOnlyList<Message> CreatePacketParts<TMessage>(
        ZlinkStreamMessageKind kind,
        ZlinkStreamRequestSeq? requestSeq,
        string packetName,
        TMessage message)
    {
        var header = new ZlinkStreamHeader(
            kind,
            ZlinkStreamCodec.Json,
            requestSeq is null ? ZlinkStreamHeaderFlags.None : ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq,
            packetName,
            ZlinkStreamMetadata.Empty,
            ZLinkStreamCorrelation.Next());
        var payload = ZLinkEnvelopeCodec.EncodeJsonBytes(message, message?.GetType() ?? typeof(TMessage));
        return
        [
            Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span),
            Message.From(payload)
        ];
    }

    private static TReply DecodeReply<TReply>(IReadOnlyList<Message> reply)
    {
        if (reply.Count == 1)
        {
            var frame = reply[0].AsReadOnlySpan();
            if (ZLinkStreamFrameCodec.TryDecode(frame, out var headerBytes, out var payload))
            {
                var header = ZLinkStreamProtocolDefaults.DecodeHeader(headerBytes.ToArray());
                return DecodeReplyPayload<TReply>(header, payload);
            }
        }

        if (reply.Count >= 2)
        {
            var header = ZLinkStreamProtocolDefaults.DecodeHeader(reply[0].AsReadOnlyMemory());
            return DecodeReplyPayload<TReply>(header, reply[1].AsReadOnlySpan());
        }

        throw new InvalidOperationException("Actor request reply is empty.");
    }

    private static TReply DecodeReplyPayload<TReply>(
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload)
    {
        if (header.Kind == ZlinkStreamMessageKind.Error)
        {
            var error = JsonSerializer.Deserialize<ZLinkStreamWireError>(
                payload,
                ZLinkJsonSerializerOptions.Default);
            if (Enum.TryParse<ZLinkFrameworkErrorKind>(error?.Code, out var kind))
                throw new ZLinkFrameworkException(
                    kind,
                    error?.Message ?? "Actor request failed.");
            throw new InvalidOperationException(error?.Message ?? "Actor request failed.");
        }

        return JsonSerializer.Deserialize<TReply>(payload, ZLinkJsonSerializerOptions.Default)
               ?? throw new InvalidOperationException("Actor request reply payload is null.");
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
        ActorRef actor,
        TMessage message) : IZLinkActorSendCall
    {
        public void Submit(CancellationToken cancellationToken = default)
        {
            ZLinkUnawaitedSubmit.Observe(
                client.SendAsync(
                    actor,
                    ZLinkMessageNameResolver.ResolveFromMessage(message),
                    message,
                    cancellationToken),
                "actor submit");
        }
    }

    private sealed class ZLinkActorRequestCall<TRequest>(
        ZLinkActorClient client,
        ActorRef actor,
        TRequest request) : IZLinkActorRequestCall
    {
        private TimeSpan? _timeout;

        public IZLinkActorRequestCall Timeout(TimeSpan timeout)
        {
            ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
            _timeout = timeout;
            return this;
        }

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        {
            return client.RequestAsync<TRequest, TReply>(
                actor,
                ZLinkMessageNameResolver.ResolveFromMessage(request),
                request,
                _timeout,
                cancellationToken);
        }
    }
}
