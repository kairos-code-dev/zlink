using Zlink.Framework.Backend.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState
{
    private static readonly ZlinkStreamLz4CompressionCodec CompressionCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly ZLinkActorPacketRegistry _packets = new();
    private readonly SemaphoreSlim _gate = new(1, 1);

    public string? SessionId { get; set; }

    public IZLinkStream? Stream { get; set; }

    public ZLinkSpotActivation? Activation { get; set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; set; }

    public ZLinkActorContext? Context { get; set; }

    public IZLinkActor? Actor { get; set; }

    public bool IsConfigured { get; set; }

    public async ValueTask ExecuteLockedAsync(
        Action operation,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            operation();
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask<T> ExecuteLockedAsync<T>(
        Func<T> operation,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            return operation();
        }
        finally
        {
            _gate.Release();
        }
    }

    public void AddPacket(IZLinkActor actor, Type handlerType, string? messageName)
    {
        _packets.Add(actor, handlerType, messageName);
    }

    public void ClearPacketRegistrations()
    {
        _packets.Clear();
    }

    public async ValueTask DispatchAsync(
        IServiceProvider services,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (!_packets.TryResolve(header, out var descriptor) || descriptor is null)
        {
            return;
        }

        if (descriptor.ActorType is not null && !descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = DecodeMessage(header, body, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var metadata = CreateMessageMetadata(services, header);
        var arguments = descriptor.ActorType is null
            ? new[] { message, CreateSendContext(services, actor.ActorId, header, metadata, cancellationToken), cancellationToken }
            : [actor, message, cancellationToken];
        var result = descriptor.HandleMethod.Invoke(handler, arguments);
        await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
    }

    public async ValueTask<byte[]> DispatchForReplyAsync(
        IServiceProvider services,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (!_packets.TryResolveRequest(header.Name, out var descriptor)
            || descriptor is null
            || descriptor.ReplyType is null)
        {
            throw new InvalidOperationException($"No actor request handler is registered for '{header.Name}'.");
        }

        var message = DecodeMessage(header, body, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var metadata = CreateMessageMetadata(services, header);
        var context = new ZLinkActorRequestContext(
            actor.ActorId,
            string.Empty,
            header.Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            services,
            cancellationToken,
            metadata);
        var result = descriptor.HandleMethod.Invoke(handler, [message, context, cancellationToken]);
        var reply = await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
        return JsonSerializer.SerializeToUtf8Bytes(reply, descriptor.ReplyType, JsonOptions);
    }

    private static object? DecodeMessage(
        ZlinkStreamHeader header,
        Message body,
        Type messageType)
    {
        if (messageType == typeof(Message))
        {
            return body;
        }

        var payload = body.AsReadOnlyMemory();
        if ((header.Flags & ZlinkStreamHeaderFlags.BodyCompressed) != 0)
        {
            payload = CompressionCodec.Decompress(payload);
        }

        if (messageType == typeof(ZlinkStreamEncodedBody))
        {
            return new ZlinkStreamEncodedBody(header.Codec, payload);
        }

        if (header.Codec == ZlinkStreamCodec.Raw)
        {
            if (messageType == typeof(string))
            {
                return Encoding.UTF8.GetString(payload.Span);
            }

            if (messageType == typeof(byte[]))
            {
                return payload.ToArray();
            }

            throw new InvalidOperationException(
                $"Raw actor packet '{header.Name}' cannot be decoded as '{messageType}'.");
        }

        if (header.Codec == ZlinkStreamCodec.Json)
        {
            return JsonSerializer.Deserialize(payload.Span, messageType, JsonOptions);
        }

        throw new InvalidOperationException(
            $"Actor packet '{header.Name}' uses codec '{header.Codec}'. Register a ZlinkStreamEncodedBody handler and decode it explicitly.");
    }

    private static ZLinkActorSendContext CreateSendContext(
        IServiceProvider services,
        string actorId,
        ZlinkStreamHeader header,
        ZLinkMessageMetadata metadata,
        CancellationToken cancellationToken)
    {
        return new ZLinkActorSendContext(
            actorId,
            string.Empty,
            header.Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            services,
            cancellationToken,
            metadata);
    }

    private static ZLinkMessageMetadata CreateMessageMetadata(
        IServiceProvider services,
        ZlinkStreamHeader header)
    {
        var policy = services.GetRequiredService<IZLinkMessageMetadataPolicy>();
        var application = new Dictionary<string, string>(StringComparer.Ordinal);

        foreach (var (key, value) in header.Metadata.Values)
        {
            if (policy.CanForwardApplicationKey(key))
            {
                application[key] = value;
            }
        }

        if (application.Count == 0)
        {
            return ZLinkMessageMetadata.Empty;
        }

        return new ZLinkMessageMetadata(
            application,
            new Dictionary<string, string>(StringComparer.Ordinal));
    }

}
