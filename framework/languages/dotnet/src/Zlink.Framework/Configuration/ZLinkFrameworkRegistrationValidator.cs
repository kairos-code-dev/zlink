namespace Zlink.Framework;

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
            ValidateOutboundCapability(
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
            ValidateOutboundCapability(
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

        if (streamNode.PacketSessionType is not null && streamNode.RawSessionType is not null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' cannot register packet and raw sessions together.");
        }

        if (streamNode.PacketSessionType is null && streamNode.RawSessionType is null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must register either a packet or raw session.");
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
            ValidateOutboundCapability(
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

    private static void ValidateOutboundCapability(
        string capabilityName,
        bool discoveryConfigured,
        IReadOnlyCollection<string> manualConnections)
    {
        var hasManualConnections = manualConnections.Count > 0;

        if (!discoveryConfigured && !hasManualConnections)
        {
            throw new ZLinkConfigurationException(
                $"{capabilityName} requires discovery or manual connections.");
        }

        if (discoveryConfigured && hasManualConnections)
        {
            throw new ZLinkConfigurationException(
                $"{capabilityName} cannot mix discovery and manual connections.");
        }
    }
}
