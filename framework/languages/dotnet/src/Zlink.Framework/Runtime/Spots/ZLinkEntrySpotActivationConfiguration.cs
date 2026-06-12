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

    private void AddActorPacketCore<THandler, TActor>(string? packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorHandlers.AddPacket(typeof(THandler), typeof(TActor), packetName);
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
