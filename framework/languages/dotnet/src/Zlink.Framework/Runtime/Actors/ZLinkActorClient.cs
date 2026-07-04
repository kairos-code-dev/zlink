using System.Buffers.Binary;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorClient(
    ZLinkFrameworkRuntime runtime,
    ZLinkStoreLocationResolvers locations) : IZLinkActorClient
{
    public IZLinkActorSendCall SendToActor<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkActorSendCall<TMessage>(this, actorId, message);
    }

    public IZLinkActorRequestCall RequestToActor<TRequest>(
        string actorId,
        TRequest request)
    {
        return new ZLinkActorRequestCall<TRequest>(this, actorId, request);
    }

    private async ValueTask SendAsync<TMessage>(
        string actorId,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        var actor = await ResolveActorRefAsync(actorId, cancellationToken).ConfigureAwait(false);
        var parts = CreatePacketParts(
            ZlinkStreamMessageKind.Send,
            null,
            packetName,
            message);
        try
        {
            await SubmitActorSendAsync(actor, parts, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception error) when (IsStaleActorError(error))
        {
            actor = await ReResolveActorRefAsync(actorId, cancellationToken).ConfigureAwait(false);
            parts = CreatePacketParts(
                ZlinkStreamMessageKind.Send,
                null,
                packetName,
                message);
            try
            {
                await SubmitActorSendAsync(actor, parts, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception retryError) when (IsStaleActorError(retryError))
            {
                throw ActorLocationStale(actorId, retryError);
            }
        }
    }

    private async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        string actorId,
        string packetName,
        TRequest request,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var actor = await ResolveActorRefAsync(actorId, cancellationToken).ConfigureAwait(false);
        var parts = CreatePacketParts(
            ZlinkStreamMessageKind.Request,
            new ZlinkStreamRequestSeq(1),
            packetName,
            request);
        try
        {
            return await SubmitActorRequestAsync<TReply>(
                    actor,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception error) when (IsStaleActorError(error))
        {
            actor = await ReResolveActorRefAsync(actorId, cancellationToken).ConfigureAwait(false);
            parts = CreatePacketParts(
                ZlinkStreamMessageKind.Request,
                new ZlinkStreamRequestSeq(1),
                packetName,
                request);
            try
            {
                return await SubmitActorRequestAsync<TReply>(
                        actor,
                        parts,
                        timeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception retryError) when (IsStaleActorError(retryError))
            {
                throw ActorLocationStale(actorId, retryError);
            }
        }
    }

    private async ValueTask<ZLinkBackendActorRef> ResolveActorRefAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var row = await locations.ResolveActorRowAsync(
                new ZLinkActorLocationKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (row?.ActorRef is { } actor) return actor.ToBackend();

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor route '{actorId}' was not found.");
    }

    private async ValueTask<ZLinkBackendActorRef> ReResolveActorRefAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var row = await locations.ResolveActorRowAsync(
                new ZLinkActorLocationKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (row?.ActorRef is { } actor) return actor.ToBackend();

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorLocationStale,
            $"Actor route '{actorId}' became stale.");
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
            if (frame.Length >= 6)
            {
                var headerSize = BinaryPrimitives.ReadUInt16BigEndian(frame[..2]);
                var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(frame.Slice(2, 4));
                if (frame.Length == 6 + headerSize + payloadSize)
                {
                    var header = ZLinkStreamProtocolDefaults.DecodeHeader(
                        frame.Slice(6, headerSize).ToArray());
                    var payload = frame.Slice(6 + headerSize, checked((int)payloadSize));
                    return DecodeReplyPayload<TReply>(header, payload);
                }
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
            throw new InvalidOperationException(error?.Message ?? "Actor request failed.");
        }

        return JsonSerializer.Deserialize<TReply>(payload, ZLinkJsonSerializerOptions.Default)
               ?? throw new InvalidOperationException("Actor request reply payload is null.");
    }

    private static bool IsStaleActorError(Exception error)
    {
        return error is ZLinkFrameworkException
        {
            Kind: ZLinkFrameworkErrorKind.ActorRouteNotFound
            or ZLinkFrameworkErrorKind.ActorLocationStale
        };
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

    private static ZLinkFrameworkException ActorLocationStale(
        string actorId,
        Exception innerException)
    {
        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorLocationStale,
            $"Actor route '{actorId}' is stale after re-resolve.",
            true,
            innerException);
    }

    private sealed class ZLinkActorSendCall<TMessage>(
        ZLinkActorClient client,
        string actorId,
        TMessage message) : IZLinkActorSendCall
    {
        private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

        public IZLinkActorSendCall PacketName(string packetName)
        {
            _packetName = packetName;
            return this;
        }

        public ValueTask Async(CancellationToken cancellationToken = default)
        {
            return client.SendAsync(
                actorId,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                message,
                cancellationToken);
        }
    }

    private sealed class ZLinkActorRequestCall<TRequest>(
        ZLinkActorClient client,
        string actorId,
        TRequest request) : IZLinkActorRequestCall
    {
        private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
        private TimeSpan? _timeout;

        public IZLinkActorRequestCall PacketName(string packetName)
        {
            _packetName = packetName;
            return this;
        }

        public IZLinkActorRequestCall Timeout(TimeSpan timeout)
        {
            _timeout = timeout;
            return this;
        }

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        {
            return client.RequestAsync<TRequest, TReply>(
                actorId,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                request,
                _timeout,
                cancellationToken);
        }
    }
}
