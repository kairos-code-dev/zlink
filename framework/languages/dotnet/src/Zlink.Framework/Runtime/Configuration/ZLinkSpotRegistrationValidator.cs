namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateSpotNode(
        ZLinkSpotNodeRegistration spotNode,
        ZLinkFrameworkRegistration registration,
        ISet<Type> globalSpotFactories,
        ISet<string> globalSpotPublisherChannels)
    {
        if (registration.SpotDiscovery is null)
        {
            ValidateSpotNodeWithoutMesh(spotNode);
        }

        if (spotNode.Router is not null && string.IsNullOrWhiteSpace(spotNode.Router.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' enables router capability but does not define a router bind endpoint.");
        }

        if (spotNode.PubSub is not null && string.IsNullOrWhiteSpace(spotNode.PubSub.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' enables pub/sub capability but does not define a pub/sub bind endpoint.");
        }

        if (spotNode.Router is null
            && spotNode.PubSub is null
            && spotNode.AttachedSpotPublisherClients.Count == 0)
        {
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' must enable router, pub/sub, or publisher-client capability.");
        }

        foreach (var acceptance in spotNode.AcceptedSpotRouteChannels.Values)
        {
            ValidateAcceptedSpotRouteChannel(spotNode, acceptance, registration);
        }

        ValidateUniqueSpotFactories(spotNode, globalSpotFactories);
        ValidateUniqueSpotPublisherChannels(spotNode, globalSpotPublisherChannels);
    }

    private static void ValidateSpotNodeWithoutMesh(ZLinkSpotNodeRegistration spotNode)
    {
        if (spotNode.PubSub is null
            && spotNode.AttachedSpotPublisherClients.Count == 0
            && spotNode.AcceptedSpotRouteChannels.Count == 0)
        {
            return;
        }

        throw new ZLinkConfigurationException(
            $"SPOT node '{spotNode.SpotNodeName}' requires AddSpotMesh(...) for mesh capabilities.");
    }

    private static void ValidateAcceptedSpotRouteChannel(
        ZLinkSpotNodeRegistration spotNode,
        ZLinkSpotRouteChannelAcceptanceRegistration acceptance,
        ZLinkFrameworkRegistration registration)
    {
        if (spotNode.Router is null)
        {
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' accepts SPOT routes from channel '{acceptance.ChannelName}' but does not enable router capability.");
        }

        var regularChannel = registration.Channels.TryGetValue(acceptance.ChannelName, out var channel)
            ? channel
            : null;
        var routeChannel = registration.RouteChannels.TryGetValue(acceptance.ChannelName, out var route)
            ? route
            : null;

        if (regularChannel is not null && routeChannel is not null)
        {
            throw new ZLinkConfigurationException(
                $"SPOT route channel '{acceptance.ChannelName}' is ambiguous because both a channel and a route mesh channel use the same name.");
        }

        if (regularChannel is null && routeChannel is null)
        {
            throw new ZLinkConfigurationException(
                $"SPOT route channel '{acceptance.ChannelName}' is not registered.");
        }

        if (regularChannel is not null
            && regularChannel.AutoConnectType != ZLinkAutoConnectType.ClientServer)
        {
            throw new ZLinkConfigurationException(
                $"SPOT route channel '{acceptance.ChannelName}' must be a client/server channel or a route mesh channel.");
        }

        if (acceptance.ManualConnections.Count == 0
            && (registration.Discovery is null || registration.Discovery.Endpoints.Count == 0))
        {
            throw new ZLinkConfigurationException(
                $"SPOT route channel '{acceptance.ChannelName}' on node '{spotNode.SpotNodeName}' requires discovery or manual connections.");
        }
    }

    private static void ValidateUniqueSpotFactories(
        ZLinkSpotNodeRegistration spotNode,
        ISet<Type> globalSpotFactories)
    {
        foreach (var spotFactory in spotNode.SpotFactories)
        {
            if (!globalSpotFactories.Add(spotFactory))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate SPOT factory '{spotFactory}' across nodes.");
            }
        }
    }

    private static void ValidateUniqueSpotPublisherChannels(
        ZLinkSpotNodeRegistration spotNode,
        ISet<string> globalSpotPublisherChannels)
    {
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
