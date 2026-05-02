namespace Zlink.Framework.Configuration;

internal static class ZLinkFrameworkRegistrationValidator
{
    public static void Validate(ZLinkFrameworkRegistration registration)
    {
        var globalSpotFactories = new HashSet<string>(StringComparer.Ordinal);
        var globalSpotPublisherChannels = new HashSet<string>(StringComparer.Ordinal);

        foreach (var channel in registration.Channels.Values)
        {
            ValidateChannel(channel, registration.Discovery is not null);
        }

        foreach (var streamNode in registration.StreamNodes.Values)
        {
            ValidateStreamNode(streamNode);
        }

        foreach (var routed in registration.RoutedChannels.Values)
        {
            ValidateRoutedChannel(routed, registration.Discovery is not null);
        }

        foreach (var spotNode in registration.SpotNodes.Values)
        {
            ValidateSpotNode(
                spotNode,
                registration,
                globalSpotFactories,
                globalSpotPublisherChannels);
        }
    }

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
                break;
            case ZLinkAutoConnectType.Fanout:
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
                break;
            case ZLinkAutoConnectType.DealerMesh:
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
                break;
            case ZLinkAutoConnectType.Invalid:
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' must declare a concrete auto connect type.");
            default:
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' has unsupported auto connect type '{channel.AutoConnectType}'.");
        }
    }

    private static void ValidateStreamNode(ZLinkStreamNodeRegistration streamNode)
    {
        if (string.IsNullOrWhiteSpace(streamNode.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must define a bind endpoint.");
        }

        if (streamNode.HeaderSessionType is null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must register a header stream session.");
        }
    }

    private static void ValidateRoutedChannel(
        ZLinkRoutedChannelRegistration routed,
        bool discoveryConfigured)
    {
        if (string.IsNullOrWhiteSpace(routed.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"Routed channel '{routed.RouterChannelId}' must define a bind endpoint.");
        }

        ZLinkPeerAcquisitionPolicy.RequireSinglePeerSource(
            $"Routed channel '{routed.RouterChannelId}'",
            discoveryConfigured,
            routed.ManualConnections);

        var keys = new HashSet<(ZLinkMessageKind Kind, string PacketName)>();
        foreach (var handler in routed.SendHandlers)
        {
            var packetName = handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType);
            if (!keys.Add((ZLinkMessageKind.Command, packetName)))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate routed send handler '{routed.RouterChannelId}:{packetName}'.");
            }
        }

        foreach (var handler in routed.RequestHandlers)
        {
            var packetName = handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType);
            if (!keys.Add((ZLinkMessageKind.Request, packetName)))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate routed request handler '{routed.RouterChannelId}:{packetName}'.");
            }
        }
    }

    private static void ValidateSpotNode(
        ZLinkSpotNodeRegistration spotNode,
        ZLinkFrameworkRegistration registration,
        ISet<string> globalSpotFactories,
        ISet<string> globalSpotPublisherChannels)
    {
        if (registration.SpotDiscovery is null)
        {
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' requires AddSpotMesh(...) or UseSpotDiscovery(...).");
        }

        foreach (var attachedChannelClient in spotNode.AttachedChannelClients.Values)
        {
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"SPOT node '{spotNode.SpotNodeName}' attached channel client '{attachedChannelClient.ChannelName}'",
                registration.Discovery is not null,
                attachedChannelClient.ManualConnections);
        }

        foreach (var spotFactory in spotNode.SpotFactories.Keys)
        {
            if (!globalSpotFactories.Add(spotFactory))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate SPOT factory '{spotFactory}' across nodes.");
            }
        }

        foreach (var publisherChannel in spotNode.AttachedSpotPublisherClients.Keys)
        {
            if (!globalSpotPublisherChannels.Add(publisherChannel))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate SPOT publisher client channel '{publisherChannel}' across nodes.");
            }
        }
    }

}
