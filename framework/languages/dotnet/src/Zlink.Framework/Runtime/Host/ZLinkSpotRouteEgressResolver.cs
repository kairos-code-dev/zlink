using Zlink.Framework.Contracts.Registry;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteEgressResolver(
    ZLinkFrameworkRegistration registration,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    Func<ZLinkFrameworkRuntimeState> stateProvider,
    Func<string, ZLinkRouteChannelRuntime> routeChannelProvider)
{
    public async ValueTask<RoutingId> ResolveTargetPeerRidAsync(
        string localEgressChannelName,
        string targetSpotNodeChannelName)
    {
        var localRoute = routeChannelProvider(localEgressChannelName);
        if (TryResolveFromDiscovery(
                localRoute,
                targetSpotNodeChannelName,
                out var discoveryPeerRid))
        {
            return discoveryPeerRid;
        }

        var registryPeerRid = await TryResolveFromRegistryQueryAsync(targetSpotNodeChannelName)
            .ConfigureAwait(false);
        if (registryPeerRid is not null)
        {
            return registryPeerRid.Value;
        }

        if (registration.RouteChannels.TryGetValue(targetSpotNodeChannelName, out var targetRoute)
            && targetRoute.RoutingConfig.RoutingId.Size > 0)
        {
            return targetRoute.RoutingConfig.RoutingId;
        }

        throw new ZLinkConfigurationException(
            $"Routed SPOT route mesh egress channel '{localEgressChannelName}' cannot find a target route peer routing id for target SPOT node channel '{targetSpotNodeChannelName}'. Configure registry discovery/query metadata for the target route mesh peer, or register the target route channel in this process with an explicit routing id for manual topology tests.");
    }

    private static bool TryResolveFromDiscovery(
        ZLinkRouteChannelRuntime localRoute,
        string targetSpotNodeChannelName,
        out RoutingId routingId)
    {
        routingId = default;
        var discovery = localRoute.Discovery;
        if (discovery is null)
        {
            return false;
        }

        HashSet<RoutingId>? candidates = null;
        var matchedPeerWithoutRoutingId = false;
        foreach (var peer in discovery.MemberPeers())
        {
            if (peer.AutoConnectType != ZLinkAutoConnectType.RouteMesh
                || peer.ServiceRole != ZLinkServiceRole.Router
                || !string.Equals(
                    peer.ChannelName,
                    targetSpotNodeChannelName,
                    StringComparison.Ordinal))
            {
                continue;
            }

            if (peer.RoutingId is not { Size: > 0 } peerRoutingId)
            {
                matchedPeerWithoutRoutingId = true;
                continue;
            }

            candidates ??= [];
            candidates.Add(peerRoutingId);
        }

        if (candidates is null || candidates.Count == 0)
        {
            if (matchedPeerWithoutRoutingId)
            {
                throw new ZLinkConfigurationException(
                    $"Routed SPOT route mesh egress channel '{localRoute.RouterChannelId}' found target channel '{targetSpotNodeChannelName}' in discovery, but the peer does not expose a routing id.");
            }

            return false;
        }

        if (candidates.Count > 1)
        {
            throw new ZLinkConfigurationException(
                $"Routed SPOT route mesh egress channel '{localRoute.RouterChannelId}' found multiple route peers for target channel '{targetSpotNodeChannelName}'. Use a unique target SPOT node route channel for this egress.");
        }

        routingId = candidates.Single();
        return true;
    }

    private async ValueTask<RoutingId?> TryResolveFromRegistryQueryAsync(
        string targetSpotNodeChannelName)
    {
        var discoveryEndpoints = registration.Discovery?.Endpoints;
        if (discoveryEndpoints is null || discoveryEndpoints.Count == 0)
        {
            return null;
        }

        var state = stateProvider();
        var registryAdapter = backendAdapterFactory.CreateRegistryAdapter();
        var candidates = new HashSet<RoutingId>();
        var matchedPeerWithoutRoutingId = false;
        foreach (var endpoint in discoveryEndpoints)
        {
            var queryClient = registryAdapter.CreateRegistryQueryClient(state.Context);
            try
            {
                queryClient.Connect(endpoint);
                foreach (var entry in queryClient.Topology(new ZLinkRegistryTopologyFilter(
                             AutoConnectType: ZLinkAutoConnectType.RouteMesh,
                             ServiceKind: ZLinkServiceKind.Socket,
                             ServiceRole: ZLinkServiceRole.Router,
                             ChannelName: targetSpotNodeChannelName,
                             State: ZLinkTopologyState.Ready)))
                {
                    if (entry.RoutingId is not { Size: > 0 } entryRoutingId)
                    {
                        matchedPeerWithoutRoutingId = true;
                        continue;
                    }

                    candidates.Add(entryRoutingId);
                }
            }
            finally
            {
                await queryClient.DisposeAsync().ConfigureAwait(false);
            }
        }

        if (candidates.Count == 0)
        {
            if (matchedPeerWithoutRoutingId)
            {
                throw new ZLinkConfigurationException(
                    $"Routed SPOT route mesh egress found target channel '{targetSpotNodeChannelName}' in registry, but the peer does not expose a routing id.");
            }

            return null;
        }

        if (candidates.Count > 1)
        {
            throw new ZLinkConfigurationException(
                $"Routed SPOT route mesh egress found multiple route peers for target channel '{targetSpotNodeChannelName}'. Use a unique target SPOT node route channel for this egress.");
        }

        return candidates.Single();
    }
}
