namespace Zlink.Framework.Runtime.Spots;

internal interface IZLinkSpotHandlerRegistrySink
{
    void AddPacket<THandler>() where THandler : class;

    void AddSubscribe<THandler>(string topic) where THandler : class;

    void AddHandler<THandler>() where THandler : class;

    void AddHandler<THandler>(string packetName) where THandler : class;

    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorSend<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorRequest<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;
}

internal interface IZLinkSpotOutboundSink
{
    IZLinkSendCall SendToSpot<TMessage>(ZLinkSpotAddress address, TMessage message);

    IZLinkRequestCall RequestToSpot<TRequest>(ZLinkSpotAddress address, TRequest request);

    IZLinkPublishCall Publish<TEvent>(string topic, TEvent message);

    IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall RequestToChannel<TRequest>(string channelName, TRequest request);
}

internal sealed class ZLinkSpotHandlerRegistrySurface(IZLinkSpotHandlerRegistrySink activation)
    : IZLinkSpotHandlerRegistry
{
    public void AddPacket<THandler>() where THandler : class
    {
        activation.AddPacket<THandler>();
    }

    public void AddSubscribe<THandler>(string topic) where THandler : class
    {
        activation.AddSubscribe<THandler>(topic);
    }

    public void AddHandler<THandler>() where THandler : class
    {
        activation.AddHandler<THandler>();
    }

    public void AddHandler<THandler>(string packetName) where THandler : class
    {
        activation.AddHandler<THandler>(packetName);
    }

    public void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
        activation.AddActorPacket<THandler, TActor>();
    }

    public void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        activation.AddActorPacket<THandler, TActor>(packetName);
    }

    public void AddActorSend<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        activation.AddActorSend<THandler, TActor>(packetName);
    }

    public void AddActorRequest<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        activation.AddActorRequest<THandler, TActor>(packetName);
    }
}

internal sealed class ZLinkSpotOutboundSurface(IZLinkSpotOutboundSink activation)
    : IZLinkSpotOutbound
{
    public IZLinkSendCall SendToSpot<TMessage>(ZLinkSpotAddress address, TMessage message)
    {
        return activation.SendToSpot(address, message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(ZLinkSpotAddress address, TRequest request)
    {
        return activation.RequestToSpot(address, request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return activation.Publish(topic, message);
    }

    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return activation.SendToChannel(channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TRequest>(string channelName, TRequest request)
    {
        return activation.RequestToChannel(channelName, request);
    }
}