using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotHandlerInvoker(IServiceProvider services, object spot)
{
    private static readonly ZlinkStreamLz4CompressionCodec CompressionCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor join handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        return await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, actor, request, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = DecodeMessage(header, body, descriptor.MessageType);
        object?[] arguments = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? [actor, message, cancellationToken]
            : [spot, actor, message, cancellationToken];

        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, arguments)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]> InvokeActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (descriptor.ReplyType is null)
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' does not declare a reply type.");
        }

        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = DecodeMessage(header, body, descriptor.MessageType);
        object?[] arguments = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? [actor, message, cancellationToken]
            : [spot, actor, message, cancellationToken];

        var reply = await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, arguments)
            .ConfigureAwait(false);
        return JsonSerializer.SerializeToUtf8Bytes(reply, descriptor.ReplyType, JsonOptions);
    }

    public async ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor lifecycle handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        object?[] arguments = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? [actor, info, cancellationToken]
            : [spot, actor, info, cancellationToken];

        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, arguments)
            .ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeAsync(
        Type handlerType,
        System.Reflection.MethodInfo method,
        object?[] arguments)
    {
        var handler = services.GetRequiredService(handlerType);
        var result = method.Invoke(handler, arguments);
        return await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
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
}
