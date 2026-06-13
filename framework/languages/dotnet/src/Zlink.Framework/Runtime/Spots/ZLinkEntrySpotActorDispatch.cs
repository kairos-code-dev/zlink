namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotActorDispatch(string spotNodeName)
{
    private ZLinkEntrySpotActivation? _activation;

    public ZLinkEntrySpotActivation? Activation => _activation;

    public void Attach(ZLinkEntrySpotActivation activation)
    {
        if (_activation is not null)
        {
            throw new InvalidOperationException($"SPOT node '{spotNodeName}' already has an Entry Spot activation.");
        }

        _activation = activation;
    }

    public bool TryResolvePacket(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        descriptor = null;
        return _activation is not null
            && _activation.TryResolveActorPacket(actorType, header, out descriptor);
    }

    public ValueTask InvokePacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return RequireActivation().InvokeActorPacketAsync(
            descriptor,
            actor,
            header,
            body,
            cancellationToken);
    }

    public ValueTask<ZLinkActorReply> InvokePacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return RequireActivation().InvokeActorPacketForReplyAsync(
            descriptor,
            actor,
            header,
            body,
            cancellationToken);
    }

    public bool TryResolveJoined(Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        descriptor = null;
        return _activation is not null
            && _activation.TryResolveActorJoined(actorType, out descriptor);
    }

    public bool TryResolveCreated(Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        descriptor = null;
        return _activation is not null
            && _activation.TryResolveActorCreated(actorType, out descriptor);
    }

    public bool TryResolveLeft(Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        descriptor = null;
        return _activation is not null
            && _activation.TryResolveActorLeft(actorType, out descriptor);
    }

    public bool TryResolveDisconnected(Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        descriptor = null;
        return _activation is not null
            && _activation.TryResolveActorDisconnected(actorType, out descriptor);
    }

    public ValueTask InvokeLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return RequireActivation().InvokeActorLifecycleAsync(
            descriptor,
            actor,
            cancellationToken);
    }

    public ValueTask InvokeDisconnectedAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return RequireActivation().InvokeActorDisconnectedAsync(
            descriptor,
            actor,
            cancellationToken);
    }

    private ZLinkEntrySpotActivation RequireActivation()
    {
        return _activation
            ?? throw new InvalidOperationException($"SPOT node '{spotNodeName}' does not have an Entry Spot.");
    }
}
