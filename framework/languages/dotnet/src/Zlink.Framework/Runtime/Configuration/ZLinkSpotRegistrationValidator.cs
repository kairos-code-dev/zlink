namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateSpotNode(
        ZLinkSpotNodeRegistration spotNode,
        ZLinkFrameworkRegistration registration,
        ISet<Type> globalSpotFactories,
        ISet<Type> globalEntrySpots)
    {
        if (registration.SpotDiscovery is null) ValidateSpotNodeWithoutMesh(spotNode);

        if (spotNode.Router is not null && string.IsNullOrWhiteSpace(spotNode.Router.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' enables router capability but does not define a router bind endpoint.");

        if (spotNode.PubSub is not null && string.IsNullOrWhiteSpace(spotNode.PubSub.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' enables pub/sub capability but does not define a pub/sub bind endpoint.");

        if (spotNode.Router is null
            && spotNode.PubSub is null)
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' must enable router or pub/sub capability.");

        if (spotNode.Router is null && spotNode.ActorFactories.Count > 0)
            throw new ZLinkConfigurationException(
                $"SPOT node '{spotNode.SpotNodeName}' requires router capability when actor factories are registered.");

        if (spotNode.Router is { } router)
        {
            router.AcquisitionMode = ZLinkPeerAcquisitionPolicy.Resolve(
                registration.Locations.Enabled,
                router.ManualConnections);
            router.ManualConnections.Freeze(router.AcquisitionMode);
        }
        if (spotNode.PubSub is { } pubSub)
        {
            pubSub.AcquisitionMode = ZLinkPeerAcquisitionPolicy.Resolve(
                registration.Locations.Enabled,
                pubSub.ManualConnections);
            pubSub.ManualConnections.Freeze(pubSub.AcquisitionMode);
        }

        ValidateUniqueSpotFactories(spotNode, globalSpotFactories);
        ValidateUniqueEntrySpot(spotNode, globalEntrySpots);
    }

    private static void ValidateSpotNodeWithoutMesh(ZLinkSpotNodeRegistration spotNode)
    {
        if (spotNode.PubSub is null) return;

        throw new ZLinkConfigurationException(
            $"SPOT node '{spotNode.SpotNodeName}' requires AddSpotMesh(...) for mesh capabilities.");
    }

    private static void ValidateUniqueSpotFactories(
        ZLinkSpotNodeRegistration spotNode,
        ISet<Type> globalSpotFactories)
    {
        foreach (var spotFactory in spotNode.SpotFactories)
            if (!globalSpotFactories.Add(spotFactory))
                throw new ZLinkConfigurationException(
                    $"Duplicate SPOT factory '{spotFactory}' across nodes.");
    }

    private static void ValidateUniqueEntrySpot(
        ZLinkSpotNodeRegistration spotNode,
        ISet<Type> globalEntrySpots)
    {
        if (spotNode.EntrySpotType is { } entrySpotType
            && !globalEntrySpots.Add(entrySpotType))
            throw new ZLinkConfigurationException(
                $"Duplicate Entry Spot '{entrySpotType}' across nodes.");
    }
}
