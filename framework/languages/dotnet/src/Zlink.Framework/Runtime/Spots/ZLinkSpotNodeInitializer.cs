using System.Security.Cryptography;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeInitializer(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkLocationLifecycle? locationLifecycle)
{
    public async ValueTask InitializeAsync(ZLinkFrameworkRuntimeState state)
    {
        if (registration.SpotNodes.Count == 0) return;

        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var spotAdapter = backendAdapterFactory.CreateSpotAdapter();

        foreach (var spotNodeRegistration in registration.SpotNodes.Values)
        {
            var node = spotAdapter.CreateSpotNode(state.Context);
            var nodeRoutingId = CreateNodeRoutingId(spotNodeRegistration);
            node.SetRoutingId(nodeRoutingId);
            node.ApplyRoleConfig(
                spotNodeRegistration.PubSub?.PublisherConfig,
                spotNodeRegistration.PubSub?.SubscriberConfig);
            if (nodeRoutingId.Size > 0 && spotNodeRegistration.PubSub is not null)
            {
                node.SetPublisherRoutingId(
                    ZLinkRoutingIdPolicy.Derive(nodeRoutingId, "pub"));
                node.SetSubscriberRoutingId(
                    ZLinkRoutingIdPolicy.Derive(nodeRoutingId, "sub"));
            }

            var nodeRuntime = new ZLinkSpotNodeRuntime(
                services,
                runtime,
                registration,
                spotNodeRegistration,
                state.Context,
                channelAdapter,
                node,
                spotNodeRegistration.SpotMeshChannelName ?? spotNodeRegistration.SpotNodeName,
                locationLifecycle);

            nodeRuntime.ApplyEntrySpotRoutingIdBeforeBind();
            if (spotNodeRegistration.Router is not null
                && spotNodeRegistration.Router.BindEndpoint is { Length: > 0 } routerEndpoint)
                node.SetRouterBind(routerEndpoint);
            if (spotNodeRegistration.PubSub is not null
                && spotNodeRegistration.PubSub.BindEndpoint is { Length: > 0 } pubEndpoint)
                node.SetPubBind(pubEndpoint);

            state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, nodeRuntime);
            try
            {
                ConnectManualPeers(spotNodeRegistration, nodeRuntime);

                await nodeRuntime.InitializeEntrySpotAsync().ConfigureAwait(false);
                await ClaimEntrySpotLocationAsync(spotNodeRegistration, node).ConfigureAwait(false);
            }
            catch (Exception initializationFailure)
            {
                state.SpotNodes.Remove(spotNodeRegistration.SpotNodeName);
                var failures = new ZLinkFailureCollector(initializationFailure);
                await failures.CaptureAsync(nodeRuntime.DisposeAsync).ConfigureAwait(false);
                failures.ThrowIfAny();
                throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
            }
        }
    }

    /// <summary>Spot lifecycle write (draft 15.1): spot node start
    /// advertises the entry spot location row. The row is keyed by the
    /// spot node's routing id — that is the identity peers know and
    /// address (session relay requests target the node rid), while the
    /// entry spot instance rid is a node-internal derivation. A failed
    /// claim is logged and never fails node startup: the row can only be
    /// missing until the store recovers, and the owner lease governs its
    /// liveness.</summary>
    private async ValueTask ClaimEntrySpotLocationAsync(
        ZLinkSpotNodeRegistration spotNodeRegistration,
        IZLinkBackendSpotNode node)
    {
        if (spotNodeRegistration.Router is null) return;

        if (locationLifecycle is not { } lifecycle) return;

        var status = await lifecycle.SpotLocations.ClaimAsync(
                spotNodeRegistration.SpotMeshChannelName ?? spotNodeRegistration.SpotNodeName,
                node.RoutingId,
                spotType: null,
                node.RoutingId,
                ZLinkSpotKind.Entry,
                spotNodeRegistration.Router.BindEndpoint,
                deactivate: null)
            .ConfigureAwait(false);
        if (status != ZLinkLocationWriteStatus.Stored)
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"entry spot location claim for '{spotNodeRegistration.SpotNodeName}' returned {status}");
    }

    private static void ConnectManualPeers(
        ZLinkSpotNodeRegistration registration,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        if (registration.Router is { AcquisitionMode: ZLinkPeerAcquisitionMode.Manual } router)
            router.ManualConnections.Attach(
            endpoint =>
            {
                _ = router.PeerRoutingIds.TryGetValue(endpoint, out var peerRid)
                    ? nodeRuntime.ConnectRouterAsync(peerRid, endpoint, CancellationToken.None)
                    : nodeRuntime.ConnectRouterAsync(endpoint, CancellationToken.None);
            },
            nodeRuntime.DisconnectRouterManual);

        if (registration.PubSub is { AcquisitionMode: ZLinkPeerAcquisitionMode.Manual } pubSub)
            pubSub.ManualConnections.Attach(
                endpoint => _ = nodeRuntime.ConnectPubSubAsync(endpoint, CancellationToken.None),
                nodeRuntime.DisconnectPubSubManual);
    }

    private static RoutingId CreateNodeRoutingId(ZLinkSpotNodeRegistration registration)
    {
        if (registration.RoutingId.Size > 0) return registration.RoutingId;

        var bytes = RandomNumberGenerator.GetBytes(16);
        bytes[0] = 0x10;
        return RoutingId.From(bytes);
    }
}
