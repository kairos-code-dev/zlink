namespace Zlink.Framework;

public interface IZLinkDiscoveryBuilder
{
    void Add(string endpoint);
}

public interface IChannelServerCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutedPeerOptions> configure);
}

public interface IChannelClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IOutboundPeerOptions> configure);

    void UseManualConnections(Action<IChannelClientConnections> configure);
}

public interface IChannelPublisherCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);
}

public interface IChannelSubscriberCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void UseManualConnections(Action<IChannelSubscriberConnections> configure);
}

public interface ISpotRouterCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutedPeerOptions> configure);

    void UseManualConnections(Action<ISpotRouterConnections> configure);
}

public interface ISpotPubSubCapabilityBuilder
{
    void ConfigurePublisherOptions(Action<ISpotNodePublisherOptions> configure);

    void ConfigureSubscriberOptions(Action<ISpotNodeSubscriberOptions> configure);

    void UseManualConnections(Action<ISpotPubSubConnections> configure);
}

public interface ISpotPublisherClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void UseManualConnections(Action<ISpotPublisherConnections> configure);
}

public interface ISpotChannelClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IOutboundPeerOptions> configure);

    void UseManualConnections(Action<IChannelClientConnections> configure);
}

public interface IZLinkStreamNodeBuilder
{
    void Bind(string endpoint);

    void AddPacketSession<TSession>()
        where TSession : class, IZLinkPacketStreamSession;

    void AddRawSession<TSession>()
        where TSession : class, IZLinkRawStreamSession;
}

public interface IZLinkChannelBuilder
{
    void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null);

    void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null);
}

public interface IZLinkSpotNodeBuilder
{
    void Bind(string endpoint);

    void EnableRouter(Action<ISpotRouterCapabilityBuilder>? configure = null);

    void EnablePubSub(Action<ISpotPubSubCapabilityBuilder>? configure = null);

    void AttachChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachSpotPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : ZLinkSpot;
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class;

    void AddChannel(
        string channelName,
        Action<IZLinkChannelBuilder> configure);

    void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure);

    void UseSpotDiscovery(
        string channelName,
        Action<IZLinkDiscoveryBuilder> configure);

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    void ConfigureDispatch(Action<IZLinkDispatchOptions> configure);

    void AddStreamNode(
        string streamNodeName,
        Action<IZLinkStreamNodeBuilder> configure);

    void AddSpotNode(
        string spotNodeName,
        Action<IZLinkSpotNodeBuilder> configure);
}
