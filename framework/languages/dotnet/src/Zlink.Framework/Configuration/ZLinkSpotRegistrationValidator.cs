namespace Zlink.Framework.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateSpotNode(
        ZLinkSpotNodeRegistration spotNode,
        ZLinkFrameworkRegistration registration,
        ISet<string> globalSpotFactories,
        ISet<string> globalSpotPublisherChannels)
    {
        if (registration.SpotDiscovery is null)
        {
            ValidateSpotNodeWithoutMesh(spotNode);
        }

        foreach (var attachedChannelClient in spotNode.AttachedChannelClients.Values)
        {
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"SPOT node '{spotNode.SpotNodeName}' attached channel client '{attachedChannelClient.ChannelName}'",
                registration.Discovery is not null,
                attachedChannelClient.ManualConnections);
        }

        ValidateUniqueSpotFactories(spotNode, globalSpotFactories);
        ValidateUniqueSpotPublisherChannels(spotNode, globalSpotPublisherChannels);
    }

    private static void ValidateSpotNodeWithoutMesh(ZLinkSpotNodeRegistration spotNode)
    {
        if (spotNode.Router is null
            && spotNode.PubSub is null
            && spotNode.AttachedChannelClients.Count == 0
            && spotNode.AttachedSpotPublisherClients.Count == 0)
        {
            return;
        }

        throw new ZLinkConfigurationException(
            $"SPOT node '{spotNode.SpotNodeName}' requires AddSpotMesh(...) or UseSpotDiscovery(...) for mesh capabilities.");
    }

    private static void ValidateUniqueSpotFactories(
        ZLinkSpotNodeRegistration spotNode,
        ISet<string> globalSpotFactories)
    {
        foreach (var spotFactory in spotNode.SpotFactories.Keys)
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
