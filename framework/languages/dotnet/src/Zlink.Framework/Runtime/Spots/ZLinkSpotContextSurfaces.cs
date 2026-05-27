namespace Zlink.Framework.Runtime.Spots;

internal interface IZLinkSpotHandlerRegistrySink
{
    void AddPacket<THandler>() where THandler : class;

    void AddSubscribe<THandler>(string topic) where THandler : class;

    void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorJoin<THandler>() where THandler : class;

    void AddHandler<THandler>() where THandler : class;

    void AddHandler<THandler>(string packetName) where THandler : class;

    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;

    void AddPostActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorDisconnected<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;
}

internal interface IZLinkSpotOutboundSink
{
    IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message);

    IZLinkRequestCall RequestSpot<TRequest>(RoutingId spotRid, TRequest request);

    IZLinkPublishCall Publish<TEvent>(string topic, TEvent message);

    IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request);
}

internal sealed class ZLinkSpotHandlerRegistrySurface(IZLinkSpotHandlerRegistrySink activation)
    : IZLinkSpotHandlerRegistry
{
    public void AddPacket<THandler>() where THandler : class
        => activation.AddPacket<THandler>();

    public void AddSubscribe<THandler>(string topic) where THandler : class
        => activation.AddSubscribe<THandler>(topic);

    public void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor
        => activation.AddActorJoin<THandler, TActor, TRequest, TReply>();

    public void AddActorJoin<THandler>() where THandler : class
        => activation.AddActorJoin<THandler>();

    public void AddHandler<THandler>() where THandler : class
        => activation.AddHandler<THandler>();

    public void AddHandler<THandler>(string packetName) where THandler : class
        => activation.AddHandler<THandler>(packetName);

    public void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
        => activation.AddActorPacket<THandler, TActor>();

    public void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
        => activation.AddActorPacket<THandler, TActor>(packetName);

    public void AddPostActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
        => activation.AddPostActorJoined<THandler, TActor>();

    public void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
        => activation.AddActorLeft<THandler, TActor>();

    public void AddActorDisconnected<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
        => activation.AddActorDisconnected<THandler, TActor>();
}

internal sealed class ZLinkSpotOutboundSurface(IZLinkSpotOutboundSink activation)
    : IZLinkSpotOutbound
{
    public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message)
        => activation.SendSpot(spotRid, message);

    public IZLinkRequestCall RequestSpot<TRequest>(RoutingId spotRid, TRequest request)
        => activation.RequestSpot(spotRid, request);

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
        => activation.Publish(topic, message);

    public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message)
        => activation.SendChannel(channelName, message);

    public IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request)
        => activation.RequestChannel(channelName, request);
}
