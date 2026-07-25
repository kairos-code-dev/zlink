namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeInitializer(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkLocationLifecycle? locationLifecycle)
{
    public async ValueTask InitializeAsync(ZLinkFrameworkComponentState state)
    {
        if (registration.SpotNodes.Count == 0) return;

        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var spotAdapter = backendAdapterFactory.CreateSpotAdapter();

        foreach (var spotNodeRegistration in registration.SpotNodes.Values)
        {
            spotNodeRegistration.EntrySpotId = CreateEntrySpotId(spotNodeRegistration);
            // Core requires the mesh membership name at construction; SpotMeshChannelName
            // is the meshName from AddRouteMesh(meshName) (falls back to the node name).
            var meshName = spotNodeRegistration.SpotMeshChannelName
                ?? spotNodeRegistration.SpotNodeName;
            var node = spotAdapter.CreateSpotNode(state.Context, meshName);
            var nodeRoutingId = CreateNodeRoutingId(spotNodeRegistration);
            node.SetRoutingId(nodeRoutingId);
            node.ApplyRoleConfig(
                spotNodeRegistration.SpotPublisherConfig,
                subscriber: null);

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

            // MeshNode startup ordering (spec 21-mesh-node §3): set routing id
            // (above), bind the ROUTER endpoint, add each configured channel
            // membership with its weight, then Start — before any spot/entry-spot
            // use. A node with no Channel role still starts so RID-direct
            // messaging remains available.
            var routerEndpoint = spotNodeRegistration.Router is { } routerRegistration
                ? ZLinkNetworkEndpointResolver.Bind(
                    routerRegistration.BindEndpoint,
                    routerRegistration.ListenPort,
                    routerRegistration.BindHost,
                    registration.NetworkOptions)
                : null;
            var hasRouterBind = routerEndpoint is { Length: > 0 };
            if (hasRouterBind)
                node.SetRouterBind(routerEndpoint!);
            if (spotNodeRegistration.Router is { } router)
            {
                node.SetMaxMessageSize(router.SocketConfig.MaxMessageSize);
                node.SetRouterHighWaterMark(router.SocketConfig.SendHighWaterMark);
                node.SetRouterSendTimeout(
                    router.SocketConfig.SendTimeout
                    ?? registration.DefaultSocketSendTimeout);
                node.SetMailboxBudgets(
                    router.SocketConfig.MailboxMessageBudget,
                    router.SocketConfig.MailboxByteBudget);
            }
            if (hasRouterBind)
            {
                // spec 05-route-mesh §4: each logical membership joins the node's
                // single ROUTER with its build-time weight (0 = excluded from new
                // select-one/multicast targeting).
                foreach (var membership in spotNodeRegistration.ChannelMemberships)
                {
                    node.AddChannel(membership.ChannelName);
                    node.SetChannelWeight(
                        membership.ChannelName,
                        membership.IsServer ? (uint)membership.Weight : 0);
                }

                node.Start();
                await WaitForLifecycleGenerationAsync(node).ConfigureAwait(false);
            }

            nodeRuntime.ApplyEntrySpotIdBeforeBind();

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

    private static async ValueTask WaitForLifecycleGenerationAsync(
        IZLinkBackendSpotNode node)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (node.MeshStatus().LifecycleGeneration == 0)
        {
            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(1), timeout.Token)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                throw new ZLinkConfigurationException(
                    $"MeshNode '{node.RoutingId}' did not publish its lifecycle generation after startup.");
            }
        }
    }

    /// <summary>
    /// Publishes the generated Entry Spot logical identity after the node
    /// owner lease is established. A conflicting global SpotId is a terminal
    /// startup error and is distinct from a MeshNode RID collision.
    /// </summary>
    private async ValueTask ClaimEntrySpotLocationAsync(
        ZLinkSpotNodeRegistration spotNodeRegistration,
        IZLinkBackendSpotNode node)
    {
        if (spotNodeRegistration.Router is null) return;

        if (locationLifecycle is not { } lifecycle) return;

        var status = await lifecycle.SpotLocations.ClaimAsync(
                spotNodeRegistration.SpotMeshChannelName ?? spotNodeRegistration.SpotNodeName,
                spotNodeRegistration.EntrySpotId,
                node.EntrySpot().LifecycleGeneration,
                spotType: null,
                node.RoutingId,
                node.MeshStatus().LifecycleGeneration,
                ZLinkSpotKind.Entry,
                authorityOwnerGeneration: 0,
                deactivate: null)
            .ConfigureAwait(false);
        if (status != ZLinkLocationWriteStatus.Stored)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotIdConflict,
                $"Entry Spot ID '{spotNodeRegistration.EntrySpotId}' could not be claimed: {status}.");
        }
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
                    ? nodeRuntime.ConnectPeerAsync(peerRid, endpoint, CancellationToken.None)
                    : nodeRuntime.ConnectPeerAsync(endpoint, CancellationToken.None);
            },
            endpoint =>
            {
                if (router.PeerRoutingIds.TryGetValue(endpoint, out var peerRid))
                    nodeRuntime.DisconnectPeerManual(endpoint, peerRid);
                else
                    nodeRuntime.DisconnectPeerManual(endpoint);
            });
    }

    private static RoutingId CreateNodeRoutingId(ZLinkSpotNodeRegistration registration)
    {
        if (registration.RoutingId.Size > 0) return registration.RoutingId;

        var prefix = registration.RoutingIdPrefix ?? registration.SpotNodeName;
        return RoutingId.From($"{prefix}-{Guid.NewGuid():D}");
    }

    private static string CreateEntrySpotId(ZLinkSpotNodeRegistration registration)
    {
        var prefix = registration.RoutingIdPrefix ?? registration.SpotNodeName;
        try
        {
            return ZLinkSpotId.CreateEntrySpotId(prefix);
        }
        catch (ArgumentException error)
        {
            throw new ZLinkConfigurationException(
                $"MeshNode '{registration.SpotNodeName}' cannot produce a valid Entry Spot ID: "
                + error.Message);
        }
    }
}
