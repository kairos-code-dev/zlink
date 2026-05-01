using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorPacketRegistry
{
    private readonly object _gate = new();
    private readonly Dictionary<(ZLinkMessageKind Kind, string Name), ZLinkActorPacketDescriptor> _packetsByName = new();

    public void Add(IZLinkActor actor, Type handlerType, string? messageName)
    {
        var descriptor = ZLinkActorPacketDescriptorFactory.Create(handlerType, actor.GetType(), messageName);
        lock (_gate)
        {
            if (_packetsByName.TryGetValue((descriptor.Kind, descriptor.MessageName), out var existing))
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

            _packetsByName.Add((descriptor.Kind, descriptor.MessageName), descriptor);
        }
    }

    public void Clear()
    {
        lock (_gate)
        {
            _packetsByName.Clear();
        }
    }

    public bool TryResolve(ZlinkStreamHeader header, out ZLinkActorPacketDescriptor? descriptor)
    {
        lock (_gate)
        {
            _packetsByName.TryGetValue((ToMessageKind(header.Kind), header.Name), out descriptor);
            if (descriptor is null && header.Kind == ZlinkStreamMessageKind.Request)
            {
                _packetsByName.TryGetValue((ZLinkMessageKind.Command, header.Name), out descriptor);
            }
        }

        return descriptor is not null;
    }

    public bool TryResolveRequest(string messageName, out ZLinkActorPacketDescriptor? descriptor)
    {
        lock (_gate)
        {
            return _packetsByName.TryGetValue((ZLinkMessageKind.Request, messageName), out descriptor);
        }
    }

    private static ZLinkMessageKind ToMessageKind(ZlinkStreamMessageKind kind)
    {
        return kind == ZlinkStreamMessageKind.Request
            ? ZLinkMessageKind.Request
            : ZLinkMessageKind.Command;
    }
}
