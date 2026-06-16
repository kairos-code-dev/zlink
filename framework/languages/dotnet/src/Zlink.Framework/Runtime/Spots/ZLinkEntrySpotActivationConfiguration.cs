namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation
{
    public void AddPacket<THandler>()
        where THandler : class
    {
        EnsureConfigurationOpen();
        _packets.Add(typeof(THandler));
    }

    public void AddSubscribe<THandler>(string topic)
        where THandler : class
    {
        EnsureConfigurationOpen();
        _subscriptions.Add(topic, typeof(THandler));
    }

    internal async ValueTask ApplyScannedHandlerAsync(
        ZLinkScannedSpotHandler handler,
        CancellationToken cancellationToken)
    {
        if (handler.SpotType != EntrySpot.GetType())
        {
            return;
        }

        EnsureConfigurationOpen();
        switch (handler.Kind)
        {
            case ZLinkScannedSpotHandlerKind.Packet:
                _packets.Add(handler.HandlerType, handler.PacketName);
                return;
            case ZLinkScannedSpotHandlerKind.Subscription:
                _subscriptions.Add(
                    handler.Topic ?? throw new InvalidOperationException("Scanned Entry Spot subscription requires a topic."),
                    handler.HandlerType);
                return;
            case ZLinkScannedSpotHandlerKind.ActorSend:
            case ZLinkScannedSpotHandlerKind.ActorRequest:
                _actorHandlers.AddPacket(
                    handler.HandlerType,
                    handler.ActorType ?? throw new InvalidOperationException("Scanned Entry Spot actor handler requires an actor type."),
                    handler.PacketName);
                return;
            case ZLinkScannedSpotHandlerKind.Timer:
                _ = await _timers.AddAsync(
                    handler.TimerName ?? throw new InvalidOperationException("Scanned Entry Spot timer requires a name."),
                    handler.TimerPeriod,
                    null,
                    handler.HandlerType,
                    EntrySpot.GetType(),
                    _stopSource.Token,
                    (descriptor, tick, ct) => ExecuteQueuedAsync(
                        static (activation, state, innerCt) => activation._invoker.InvokeTimerAsync(
                            state.Descriptor,
                            state.Tick,
                            innerCt),
                        (Descriptor: descriptor, Tick: tick),
                        ct),
                    PublishTimerFailureAsync,
                    cancellationToken).ConfigureAwait(false);
                return;
            default:
                throw new InvalidOperationException($"Unsupported scanned Entry Spot handler kind '{handler.Kind}'.");
        }
    }

    public void AddHandler<THandler>()
        where THandler : class
    {
        EnsureConfigurationOpen();
        _actorHandlers.AddHandler(typeof(THandler), null);
    }

    public void AddHandler<THandler>(string packetName)
        where THandler : class
    {
        if (string.IsNullOrWhiteSpace(packetName))
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        EnsureConfigurationOpen();
        _actorHandlers.AddHandler(typeof(THandler), packetName);
    }

    public void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        AddActorPacketCore<THandler, TActor>(null);
    }

    public void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        if (string.IsNullOrWhiteSpace(packetName))
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        AddActorPacketCore<THandler, TActor>(packetName);
    }

    public void AddActorSend<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        if (string.IsNullOrWhiteSpace(packetName))
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        AddActorPacketCore<THandler, TActor>(packetName, ZLinkMessageKind.Command);
    }

    public void AddActorRequest<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        if (string.IsNullOrWhiteSpace(packetName))
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        AddActorPacketCore<THandler, TActor>(packetName, ZLinkMessageKind.Request);
    }

    private void AddActorPacketCore<THandler, TActor>(string? packetName)
        where THandler : class
        where TActor : IZLinkActor =>
        AddActorPacketCore<THandler, TActor>(packetName, expectedKind: null);

    private void AddActorPacketCore<THandler, TActor>(
        string? packetName,
        ZLinkMessageKind? expectedKind)
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorHandlers.AddPacket(typeof(THandler), typeof(TActor), packetName, expectedKind);
    }

    private void EnsureConfigurationOpen()
    {
        if (!_configurationOpen)
        {
            throw new InvalidOperationException(
                "Entry Spot handler registration is only allowed while Configure is running.");
        }
    }
}
