namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    public void AttachSpot(IZLinkSpot spot)
    {
        ArgumentNullException.ThrowIfNull(spot);
        if (_spot is not null)
        {
            throw new InvalidOperationException("SPOT has already been attached to this context.");
        }

        if (!ReferenceEquals(spot.Context, this))
        {
            throw new InvalidOperationException(
                $"SPOT '{spot.GetType().FullName}' must expose the context provided by the runtime.");
        }

        _spot = spot;
        _actorHandlers = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.UserSpot,
            spot.GetType());
        _handlerInvoker = new ZLinkSpotHandlerInvoker(_scope.ServiceProvider, spot);
    }

    public void BindDescriptors()
    {
        _configurationOpen = false;

        _packets.Bind(Spot);
        _subscriptions.Bind(Spot, NativeSpot);
        _actorJoins.Bind(Spot);
        _actorHandlers?.Bind();
    }

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
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        EnsureConfigurationOpen();
        RequireActorHandlers().AddHandler(typeof(THandler), packetName);
    }

    public void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorJoins.Add(
            typeof(THandler),
            typeof(TActor),
            typeof(TRequest),
            typeof(TReply));
    }

    public void AddActorJoin<THandler>()
        where THandler : class
    {
        EnsureConfigurationOpen();
        _actorJoins.Add(typeof(THandler));
    }

    public void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        AddActorPacketCore<THandler, TActor>(packetName: null);
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

    public void AddPostActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        RequireActorHandlers().AddJoined(typeof(THandler), typeof(TActor));
    }

    public void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        RequireActorHandlers().AddLeft(typeof(THandler), typeof(TActor));
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
