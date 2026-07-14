namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
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

    public void AddHandler<THandler>()
        where THandler : class
    {
        EnsureConfigurationOpen();
        RequireActorHandlers().AddHandler(typeof(THandler), null);
    }

    public void AddHandler<THandler>(string packetName)
        where THandler : class
    {
        if (string.IsNullOrWhiteSpace(packetName))
            throw new InvalidOperationException("Actor packet name must not be empty.");

        EnsureConfigurationOpen();
        RequireActorHandlers().AddHandler(typeof(THandler), packetName);
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
            throw new InvalidOperationException("Actor packet name must not be empty.");

        AddActorPacketCore<THandler, TActor>(packetName);
    }

    public void AttachSpot(IZLinkSpot spot)
    {
        ArgumentNullException.ThrowIfNull(spot);
        if (_spot is not null) throw new InvalidOperationException("SPOT has already been attached to this context.");

        if (!ReferenceEquals(spot.Context, this))
            throw new InvalidOperationException(
                $"SPOT '{spot.GetType().FullName}' must expose the context provided by the runtime.");

        _spot = spot;
        _actorHandlers = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.UserSpot,
            spot.GetType());
        _handlerInvoker = new ZLinkSpotHandlerInvoker(
            _handlerInstances,
            spot,
            _runtime.Registration.Codecs,
            _runtime.Registration.StreamCompressionCodec);
    }

    public void BindDescriptors()
    {
        _configurationOpen = false;

        _packets.Bind(Spot);
        _subscriptions.Bind(Spot, NativeSpot);
        _actorJoins.Bind(Spot);
        _actorHandlers?.Bind();
    }

    internal async ValueTask ApplyScannedHandlerAsync(
        ZLinkScannedSpotHandler handler,
        CancellationToken cancellationToken)
    {
        if (handler.SpotType != Spot.GetType()) return;

        EnsureConfigurationOpen();
        switch (handler.Kind)
        {
            case ZLinkScannedSpotHandlerKind.Packet:
                _packets.Add(handler);
                return;
            case ZLinkScannedSpotHandlerKind.Subscription:
                if (handler.SpotNodeName is not null
                    && !string.Equals(handler.SpotNodeName, SpotNodeName, StringComparison.Ordinal)) return;
                var topic = handler.Topic
                            ?? throw new InvalidOperationException("Scanned SPOT subscription requires a topic.");
                if (handler.Method is { } subscriptionMethod)
                    _subscriptions.Add(topic, handler.HandlerType, subscriptionMethod);
                else
                    _subscriptions.Add(topic, handler.HandlerType);
                return;
            case ZLinkScannedSpotHandlerKind.ActorSend:
            case ZLinkScannedSpotHandlerKind.ActorRequest:
                RequireActorHandlers().AddPacket(
                    handler.HandlerType,
                    handler.ActorType ??
                    throw new InvalidOperationException("Scanned SPOT actor handler requires an actor type."),
                    handler.PacketName);
                return;
            case ZLinkScannedSpotHandlerKind.Timer:
                _ = await _timers.AddAsync(
                    handler.TimerName ?? throw new InvalidOperationException("Scanned SPOT timer requires a name."),
                    handler.TimerPeriod,
                    null,
                    handler.HandlerType,
                    Spot.GetType(),
                    StopToken,
                    (descriptor, tick, ct) => ExecuteSerializedAsync(
                        async static (activation, state, innerCt) =>
                        {
                            await activation.InvokeTimerAsync(state.Descriptor, state.Tick, innerCt);
                        },
                        (Descriptor: descriptor, Tick: tick),
                        ct),
                    PublishTimerFailureAsync,
                    cancellationToken).ConfigureAwait(false);
                return;
            default:
                throw new InvalidOperationException($"Unsupported scanned SPOT handler kind '{handler.Kind}'.");
        }
    }

    private void AddActorPacketCore<THandler, TActor>(string? packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        RequireActorHandlers().AddPacket(typeof(THandler), typeof(TActor), packetName);
    }

    private ZLinkSpotActorHandlerRegistry RequireActorHandlers()
    {
        return _actorHandlers
               ?? throw new InvalidOperationException("SPOT actor registry is not initialized.");
    }
}
