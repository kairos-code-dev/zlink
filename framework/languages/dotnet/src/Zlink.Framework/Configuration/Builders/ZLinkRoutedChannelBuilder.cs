namespace Zlink.Framework.Configuration.Builders;

internal sealed class ZLinkRoutedChannelBuilder(ZLinkRoutedChannelRegistration registration)
    : IZLinkRoutedChannelBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Routed channel bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure)
    {
        configure(registration.SocketOptions);
    }

    public void ConfigureRouting(Action<IRoutedPeerOptions> configure)
    {
        configure(registration.RoutingOptions);
    }

    public void UseManualConnections(Action<IRoutedChannelConnections> configure)
    {
        configure(new ZLinkMutableConnections(registration.ManualConnections));
    }

    public void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRoutedSendHandler<TMessage>
    {
        registration.SendHandlers.Add(new ZLinkRoutedHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
    }

    public void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRoutedRequestHandler<TRequest, TReply>
    {
        registration.RequestHandlers.Add(new ZLinkRoutedHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
    }

    public void EnableSessionGateway()
    {
        registration.SessionGatewayEnabled = true;
    }

    public void AddSessionProxyHandler<THandler>()
        where THandler : class, IZLinkSessionProxyHandler
    {
        registration.SessionProxyHandlerType = typeof(THandler);
    }
}
