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
            var entrySpotId = CreateEntrySpotId(spotNodeRegistration);
            // Core requires the mesh membership name at construction; SpotMeshChannelName
            // is the meshName from AddRouteMesh(meshName) (falls back to the node name).
            var meshName = spotNodeRegistration.SpotMeshChannelName
                ?? spotNodeRegistration.SpotNodeName;
            var node = spotAdapter.CreateSpotNode(state.Context, meshName);
            var nodeRoutingId = PrepareNodeRoutingId(spotNodeRegistration);
            node.SetRoutingId(nodeRoutingId);
            node.SetObjectRole(spotNodeRegistration.ObjectRole);
            node.ApplyRoleConfig(
                spotNodeRegistration.SpotPublisherConfig,
                subscriber: null);

            var routerEndpoint = spotNodeRegistration.Router is { } routerRegistration
                ? ZLinkNetworkEndpointResolver.Bind(
                    routerRegistration.BindEndpoint,
                    routerRegistration.ListenPort,
                    routerRegistration.BindHost,
                    registration.NetworkOptions)
                : null;
            var hasRouterBind = routerEndpoint is { Length: > 0 };
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
            foreach (var membership in spotNodeRegistration.ChannelMemberships)
            {
                if (!membership.IsServer)
                    continue;
                node.AddChannel(membership.ChannelName);
                node.SetChannelWeight(membership.ChannelName, (uint)membership.Weight);
            }

            ZLinkMeshNodeStartupState? startupState = null;
            ZLinkSpotNodeRuntime? nodeRuntime = null;
            try
            {
                if (RequiresAutomaticDescriptorClaim(spotNodeRegistration))
                {
                    var lifecycle = locationLifecycle
                        ?? throw new ZLinkConfigurationException(
                            $"MeshNode '{spotNodeRegistration.SpotNodeName}' requires a Location Store.");
                    var descriptor = ZLinkMeshNodeDescriptorFactory.Create(
                        registration,
                        spotNodeRegistration,
                        nodeRoutingId,
                        node.MeshStatus().LifecycleGeneration,
                        endpoint: string.Empty,
                        entrySpotId,
                        descriptorRevision: 1,
                        ZLinkFrameworkRuntimeState.Preparing);
                    var claim = await lifecycle.WriteMeshNodeDescriptorAsync(
                            descriptor,
                            ZLinkLocationWriteIntent.NewClaim)
                        .ConfigureAwait(false);
                    if (claim.Status != ZLinkLocationWriteStatus.Stored)
                    {
                        if (claim.Status != ZLinkLocationWriteStatus.RejectedConflict)
                            throw new ZLinkConfigurationException(
                                $"MeshNode '{spotNodeRegistration.SpotNodeName}' could not "
                                + $"claim its automatic descriptor: {claim.Status}.");
                        var conflictKind = descriptor.EntrySpotId is not null
                            ? await lifecycle.ClassifyMeshNodeClaimConflictAsync(
                                    meshName,
                                    nodeRoutingId,
                                    descriptor.EntrySpotId)
                                .ConfigureAwait(false)
                            : ZLinkFrameworkErrorKind.AlreadyExists;
                        throw CreateClaimFailure(
                            spotNodeRegistration,
                            nodeRoutingId,
                            entrySpotId,
                            claim.Status,
                            conflictKind);
                    }
                    startupState = new ZLinkMeshNodeStartupState(
                        nodeRoutingId,
                        entrySpotId,
                        descriptor,
                        claim.Generation);
                }

                if (hasRouterBind)
                    node.SetRouterBind(routerEndpoint!);
                if (hasRouterBind)
                    node.Start();

                if (startupState is not null)
                {
                    var boundRouter = spotNodeRegistration.Router!;
                    var actualEndpoint = ZLinkNetworkEndpointResolver.Advertise(
                        node.Status().LocalEndpoint ?? routerEndpoint!,
                        boundRouter.AdvertiseHost,
                        boundRouter.BindHost,
                        registration.NetworkOptions)
                        ?? throw new ZLinkConfigurationException(
                            $"MeshNode '{spotNodeRegistration.SpotNodeName}' did not expose an advertised endpoint.");
                    var boundDescriptor = startupState.Descriptor with
                    {
                        Endpoint = actualEndpoint,
                        DescriptorRevision = 2
                    };
                    var renewed = await locationLifecycle!
                        .WriteMeshNodeDescriptorAsync(
                            boundDescriptor,
                            ZLinkLocationWriteIntent.Renew)
                        .ConfigureAwait(false);
                    if (renewed.Status != ZLinkLocationWriteStatus.Stored)
                        throw new ZLinkConfigurationException(
                            $"MeshNode '{spotNodeRegistration.SpotNodeName}' could not finalize "
                            + $"its claimed descriptor: {renewed.Status}.");
                    startupState = startupState with
                    {
                        Descriptor = boundDescriptor,
                        StoreGeneration = renewed.Generation
                    };
                }

                nodeRuntime = new ZLinkSpotNodeRuntime(
                    services,
                    runtime,
                    registration,
                    spotNodeRegistration,
                    state.Context,
                    channelAdapter,
                    node,
                    meshName,
                    locationLifecycle,
                    startupState,
                    entrySpotId);
                nodeRuntime.ApplyEntrySpotIdBeforeBind();
                state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, nodeRuntime);
                ConnectManualPeers(spotNodeRegistration, nodeRuntime);

                await nodeRuntime.InitializeEntrySpotAsync().ConfigureAwait(false);
            }
            catch (Exception initializationFailure)
            {
                state.SpotNodes.Remove(spotNodeRegistration.SpotNodeName);
                var failures = new ZLinkFailureCollector(initializationFailure);
                if (startupState is not null && locationLifecycle is not null)
                {
                    await failures.CaptureAsync(
                            async () =>
                            {
                                _ = await locationLifecycle.RemoveMeshNodeDescriptorAsync(
                                        new ZLinkMeshNodeDescriptorKey(
                                            startupState.Descriptor.MeshName,
                                            startupState.RoutingId))
                                    .ConfigureAwait(false);
                            })
                        .ConfigureAwait(false);
                }
                if (nodeRuntime is not null)
                    await failures.CaptureAsync(nodeRuntime.DisposeAsync).ConfigureAwait(false);
                else
                    await failures.CaptureAsync(node.DisposeAsync).ConfigureAwait(false);
                failures.ThrowIfAny();
                throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
            }
        }
    }

    private static bool RequiresAutomaticDescriptorClaim(
        ZLinkSpotNodeRegistration registration) =>
        !registration.HasExplicitRoutingId
        && registration.Router?.AcquisitionMode
            == ZLinkPeerAcquisitionMode.AutoConnect;

    private static Exception CreateClaimFailure(
        ZLinkSpotNodeRegistration registration,
        RoutingId routingId,
        string entrySpotId,
        ZLinkLocationWriteStatus status,
        ZLinkFrameworkErrorKind conflictKind) =>
        new ZLinkFrameworkException(
            conflictKind,
            $"MeshNode '{registration.SpotNodeName}' could not claim automatic RID "
            + $"'{routingId}' with Entry Spot ID '{entrySpotId}': {status}.");

    private static void ConnectManualPeers(
        ZLinkSpotNodeRegistration registration,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        if (registration.Router is { AcquisitionMode: ZLinkPeerAcquisitionMode.Manual } router)
            nodeRuntime.OwnManualConnectionAttachment(router.ManualConnections.Attach(
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
            }));
    }

    internal static RoutingId PrepareNodeRoutingId(
        ZLinkSpotNodeRegistration registration)
    {
        if (registration.RoutingId.Size > 0) return registration.RoutingId;

        if (registration.PreparedRoutingId.Size > 0)
            return registration.PreparedRoutingId;

        var prefix = registration.RoutingIdPrefix ?? registration.SpotNodeName;
        registration.PreparedRoutingId =
            RoutingId.From($"{prefix}-{Guid.NewGuid():D}");
        return registration.PreparedRoutingId;
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
