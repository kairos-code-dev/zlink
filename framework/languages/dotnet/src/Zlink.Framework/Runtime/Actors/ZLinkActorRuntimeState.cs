using Zlink.Framework.Backend.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using System.Reflection;
using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState
{
    private static readonly ZlinkStreamPacketNameResolver MessageNameResolver = new();
    private static readonly ZlinkStreamLz4CompressionCodec CompressionCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly object _packetGate = new();
    private readonly Dictionary<string, ZLinkActorPacketDescriptor> _packetsByName = new(StringComparer.Ordinal);

    public SemaphoreSlim Gate { get; } = new(1, 1);

    public string? SessionId { get; set; }

    public IZLinkStream? Stream { get; set; }

    public ZLinkSpotActivation? Activation { get; set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; set; }

    public ZLinkActorContext? Context { get; set; }

    public IZLinkActor? Actor { get; set; }

    public bool IsConfigured { get; set; }

    public void AddPacket(IZLinkActor actor, Type handlerType, string? messageName)
    {
        var descriptor = CreatePacketDescriptor(handlerType, actor.GetType(), messageName);
        lock (_packetGate)
        {
            if (_packetsByName.TryGetValue(descriptor.MessageName, out var existing))
            {
                if (existing.HandlerType == descriptor.HandlerType
                    && existing.MessageType == descriptor.MessageType
                    && existing.ActorType == descriptor.ActorType)
                {
                    return;
                }

                throw new InvalidOperationException(
                    $"Actor packet '{descriptor.MessageName}' is already registered on '{actor.GetType()}'.");
            }

            _packetsByName.Add(descriptor.MessageName, descriptor);
        }
    }

    public void ClearPacketRegistrations()
    {
        lock (_packetGate)
        {
            _packetsByName.Clear();
        }
    }

    public async ValueTask DispatchAsync(
        IServiceProvider services,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        ZLinkActorPacketDescriptor? descriptor;
        lock (_packetGate)
        {
            _packetsByName.TryGetValue(header.Name, out descriptor);
        }

        if (descriptor is null)
        {
            return;
        }

        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = DecodeMessage(header, body, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [actor, message, cancellationToken]);
        await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
    }

    private static ZLinkActorPacketDescriptor CreatePacketDescriptor(
        Type handlerType,
        Type expectedActorType,
        string? messageName)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkActorPacketHandler<,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            if (!arguments[0].IsAssignableFrom(expectedActorType))
            {
                throw new InvalidOperationException(
                    $"Actor packet handler '{handlerType}' targets '{arguments[0]}', but the runtime actor type is '{expectedActorType}'.");
            }

            return new ZLinkActorPacketDescriptor
            {
                HandlerType = handlerType,
                ActorType = arguments[0],
                MessageType = arguments[1],
                HandleMethod = handlerType.GetMethod("HandleAsync", BindingFlags.Instance | BindingFlags.Public)!,
                MessageName = messageName ?? MessageNameResolver.Resolve(arguments[1])
            };
        }

        throw new InvalidOperationException(
            $"Actor packet handler '{handlerType}' must implement IZLinkActorPacketHandler<,>.");
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
