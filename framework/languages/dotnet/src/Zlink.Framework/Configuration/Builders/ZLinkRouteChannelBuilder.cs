namespace Zlink.Framework.Configuration.Builders;

internal sealed class ZLinkRouteChannelBuilder(ZLinkRouteChannelRegistration registration)
    : IZLinkRouteChannelBuilder, IZLinkRouteMeshChannelBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Route channel bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void ConfigureRouting(Action<IZLinkRoutePolicyOptions> configure)
    {
        configure(registration.RoutingOptions);
    }

    public void UseManualConnections(Action<IRouteChannelConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }

    public void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>
    {
        registration.SendHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
    }

    public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>
    {
        registration.RequestHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
    }

}
