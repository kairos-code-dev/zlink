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
                $"MeshNode '{spotNode.SpotNodeName}' must define its ROUTER endpoint via Listen(...).");

        // spec 05-route-mesh §2: a MeshNode must register at least one logical
        // channel membership (ChannelName). Memberships join the node's ROUTER at
        // startup, so a serving node with none cannot Start.
        if (spotNode.Router is not null && spotNode.ChannelMemberships.Count == 0)
            throw new ZLinkConfigurationException(
                $"MeshNode '{spotNode.SpotNodeName}' must register at least one channel via ChannelName(...).");

        if (spotNode.Router is null && spotNode.ActorFactories.Count > 0)
            throw new ZLinkConfigurationException(
                $"MeshNode '{spotNode.SpotNodeName}' requires a ROUTER endpoint when actor factories are registered.");

        // S8-04B fail-fast (spec 05-route-mesh §7): distributed Spot/Actor
        // addressing and Actor transfer resolve peers through a location store.
        // A production host that registers Spots, Actors, an Entry Spot or a
        // transfer adapter without any store fails startup here; single-process
        // contract tests opt in with the in-memory store (Locations.Enabled).
        if (!registration.Locations.Enabled && SpotNodeUsesDistributedLookup(spotNode))
            throw new ZLinkConfigurationException(
                $"MeshNode '{spotNode.SpotNodeName}' uses distributed Spot/Actor addressing or Actor transfer "
                + "but no location store is registered. Register the production store via AddLocationStore(...) "
                + "(spec 05-route-mesh §7).");

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
            $"MeshNode '{spotNode.SpotNodeName}' requires AddRouteMesh(...) for mesh capabilities.");
    }

    private static bool SpotNodeUsesDistributedLookup(ZLinkSpotNodeRegistration spotNode)
    {
        return spotNode.SpotFactories.Count > 0
               || spotNode.EntrySpotType is not null
               || spotNode.ActorFactories.Count > 0
               || spotNode.ActorTransfers.Count > 0;
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
