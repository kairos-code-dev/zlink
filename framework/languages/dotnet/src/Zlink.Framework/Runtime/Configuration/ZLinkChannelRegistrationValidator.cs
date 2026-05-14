namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateChannel(
        ZLinkChannelRegistration channel,
        bool discoveryConfigured)
    {
        ValidateChannelShape(channel);

        if (channel.Server is not null && string.IsNullOrWhiteSpace(channel.Server.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' server must define a bind endpoint.");
        }

        if (channel.Client is not null)
        {
            if (channel.AutoConnectType != ZLinkAutoConnectType.DealerMesh
                && !string.IsNullOrWhiteSpace(channel.Client.BindEndpoint))
            {
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' client bind endpoint is only valid for dealer mesh channels.");
            }

            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' client",
                discoveryConfigured,
                channel.Client.ManualConnections);
        }

        if (channel.Publisher is not null && string.IsNullOrWhiteSpace(channel.Publisher.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' publisher must define a bind endpoint.");
        }

        if (channel.Subscriber is not null)
        {
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' subscriber",
                discoveryConfigured,
                channel.Subscriber.ManualConnections);
        }
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
            case ZLinkAutoConnectType.DealerMesh:
                RequireDealerMeshShape(channel);
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
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' must enable server or client capabilities.");
        }

        if (channel.Publisher is not null || channel.Subscriber is not null)
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot enable publisher or subscriber capabilities.");
        }
    }

    private static void RequireFanoutShape(ZLinkChannelRegistration channel)
    {
        if (channel.Publisher is null && channel.Subscriber is null)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' must enable publisher or subscriber capabilities.");
        }

        if (channel.Server is not null || channel.Client is not null)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot enable server or client capabilities.");
        }
    }

    private static void RequireDealerMeshShape(ZLinkChannelRegistration channel)
    {
        if (channel.Client is null)
        {
            throw new ZLinkConfigurationException(
                $"dealer mesh channel '{channel.ChannelName}' must enable client capabilities.");
        }

        if (channel.Server is not null || channel.Publisher is not null || channel.Subscriber is not null)
        {
            throw new ZLinkConfigurationException(
                $"dealer mesh channel '{channel.ChannelName}' can only enable client capabilities.");
        }
    }
}
