namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateChannel(
        ZLinkChannelRegistration channel,
        bool autoConnectConfigured,
        ZLinkHandlerExposureCatalog handlerExposure)
    {
        ValidateChannelShape(channel);

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

        ValidateFanoutHandlerExposure(channel, handlerExposure.ValidateChannel(channel));
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
        if (channel.AutoConnectType != ZLinkLocationAutoConnectType.Fanout)
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' must declare fanout topology.");
        if (channel.Publisher is null && channel.Subscriber is null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' must enable publisher or subscriber capabilities.");
    }
}
