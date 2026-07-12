namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateChannel(
        ZLinkChannelRegistration channel,
        bool autoConnectConfigured,
        ZLinkHandlerExposureCatalog handlerExposure)
    {
        ValidateChannelShape(channel);

        if (channel.Server is not null && string.IsNullOrWhiteSpace(channel.Server.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' server must define a bind endpoint.");

        if (channel.Client is not null)
        {
            if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint))
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' client cannot define a bind endpoint.");

            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' client",
                autoConnectConfigured,
                channel.Client.ManualConnections);
            channel.Client.AcquisitionMode = ZLinkPeerAcquisitionPolicy.Resolve(
                autoConnectConfigured,
                channel.Client.ManualConnections);
            channel.Client.ManualConnections.Freeze(channel.Client.AcquisitionMode);
        }

        if (channel.Publisher is not null && string.IsNullOrWhiteSpace(channel.Publisher.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' publisher must define a bind endpoint.");

        if (channel.Subscriber is not null)
        {
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' subscriber",
                autoConnectConfigured,
                channel.Subscriber.ManualConnections);
            channel.Subscriber.AcquisitionMode = ZLinkPeerAcquisitionPolicy.Resolve(
                autoConnectConfigured,
                channel.Subscriber.ManualConnections);
            channel.Subscriber.ManualConnections.Freeze(channel.Subscriber.AcquisitionMode);
        }

        var exposedKinds = handlerExposure.ValidateChannel(channel);
        ValidateChannelHandlerCapabilities(channel, exposedKinds);
    }

    private static void ValidateChannelHandlerCapabilities(
        ZLinkChannelRegistration channel,
        IReadOnlySet<ZLinkMessageKind> exposedKinds)
    {
        switch (channel.AutoConnectType)
        {
            case ZLinkAutoConnectType.ClientServer:
                ValidateClientServerHandlerExposure(channel, exposedKinds);
                break;
            case ZLinkAutoConnectType.Fanout:
                ValidateFanoutHandlerExposure(channel, exposedKinds);
                break;
        }
    }

    private static void ValidateClientServerHandlerExposure(
        ZLinkChannelRegistration channel,
        IReadOnlySet<ZLinkMessageKind> exposedKinds)
    {
        var hasHandlerExposure = exposedKinds.Contains(ZLinkMessageKind.Command)
                                 || exposedKinds.Contains(ZLinkMessageKind.Request);

        if (hasHandlerExposure && channel.Server is null)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' exposes handlers but does not enable server capability.");

        if (channel.Server is not null
            && !hasHandlerExposure)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' server must map a handler group, register a typed handler, or be accepted by a SPOT route channel.");

        if (channel.PublishHandlers.Count > 0)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot register publish handlers.");
    }

    private static void ValidateFanoutHandlerExposure(
        ZLinkChannelRegistration channel,
        IReadOnlySet<ZLinkMessageKind> exposedKinds)
    {
        var hasHandlerExposure = exposedKinds.Contains(ZLinkMessageKind.Publish);

        if (hasHandlerExposure && channel.Subscriber is null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' exposes publish handlers but does not enable subscriber capability.");

        if (channel.Subscriber is not null && !hasHandlerExposure)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' subscriber must map a publish handler group or register a typed publish handler.");

        if (channel.SendHandlers.Count > 0 || channel.RequestHandlers.Count > 0)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot register send or request handlers.");
    }

    private static void ValidateChannelShape(ZLinkChannelRegistration channel)
    {
        switch (channel.AutoConnectType)
        {
            case ZLinkAutoConnectType.ClientServer:
                RequireClientServerShape(channel);
                break;
            case ZLinkAutoConnectType.Fanout:
                RequireFanoutShape(channel);
                break;
            case ZLinkAutoConnectType.Invalid:
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' must declare a concrete auto connect type.");
            default:
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' has unsupported auto connect type '{channel.AutoConnectType}'.");
        }
    }

    private static void RequireClientServerShape(ZLinkChannelRegistration channel)
    {
        if (channel.Server is null && channel.Client is null)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' must enable server or client capabilities.");

        if (channel.Publisher is not null || channel.Subscriber is not null)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot enable publisher or subscriber capabilities.");
    }

    private static void RequireFanoutShape(ZLinkChannelRegistration channel)
    {
        if (channel.Publisher is null && channel.Subscriber is null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' must enable publisher or subscriber capabilities.");

        if (channel.Server is not null || channel.Client is not null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot enable server or client capabilities.");
    }
}
