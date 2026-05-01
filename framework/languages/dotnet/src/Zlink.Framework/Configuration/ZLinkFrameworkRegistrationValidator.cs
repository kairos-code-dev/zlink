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
        if (channel.Server is not null && string.IsNullOrWhiteSpace(channel.Server.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' server must define a bind endpoint.");
        }

        if (channel.Client is not null)
        {
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
                $"SPOT node '{spotNode.SpotNodeName}' requires UseSpotDiscovery(...).");
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
